// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "folder_widget.h"

#include "constants.h"
#include "model/diff/diff_assembler.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/diff/modify/remove_folder.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/suspend_folder.h"
#include "model/diff/modify/unshare_folder.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/local/scan_request.h"
#include "file_matching_widget.h"
#include "presence_item.h"
#include "presentation/folder_presence.h"
#include "proto/proto-helpers-db.h"
#include "proto/proto-helpers-db.h"
#include "static_table.h"
#include "table_widget/checkbox.h"
#include "table_widget/choice.h"
#include "table_widget/input.h"
#include "table_widget/int_input.h"
#include "table_widget/label.h"
#include "table_widget/path.h"
#include "utils.hpp"
#include "utils/format.hpp"

#include <FL/Fl_Tabs.H>
#include <FL/platform.H>
#include <FL/fl_ask.H>
#include <charconv>

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::presentation;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::content;

namespace {

#define ENABLE_ACTION(B, FLAG)                                                                                         \
    if (B) {                                                                                                           \
        if (actions & FLAG) {                                                                                          \
            B->activate();                                                                                             \
        } else {                                                                                                       \
            B->deactivate();                                                                                           \
        }                                                                                                              \
    }

static constexpr int padding = 2;

using buttons_mask_t = std::uint32_t;

static auto constexpr B_CREATE = buttons_mask_t{1 << 0};
static auto constexpr B_APPLY = buttons_mask_t{1 << 1};
static auto constexpr B_SHARE = buttons_mask_t{1 << 2};
static auto constexpr B_RESCAN = buttons_mask_t{1 << 3};
static auto constexpr B_RESET = buttons_mask_t{1 << 4};
static auto constexpr B_REMOVE = buttons_mask_t{1 << 5};

using ctx_t = folder_widget_t::serialization_context_t;

struct base_table_t;

struct checkbox_widget_t : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;
    using parent_t::parent_t;

    Fl_Widget *create_widget(int x, int y, int w, int h) override;
};

struct device_share_widget_t final : widgetable_t {
    using parent_t = widgetable_t;
    device_share_widget_t(Fl_Widget &container, model::device_ptr_t device_);

    Fl_Widget *create_widget(int x, int y, int w, int h) override;
    void reset() override;
    bool store(void *data) override;

    model::device_ptr_t initial_device;
    model::device_ptr_t device;
    Fl_Choice *input;
    bool disabled;
};

struct base_table_t : syncspirit::fltk::static_table_t {
    using parent_t = syncspirit::fltk::static_table_t;
    using B = content::folder_widget_t::behavior_t;

    base_table_t(folder_widget_t &container_, int x, int y, int w, int h)
        : parent_t(x, y, w, h), container{container_} {}

    inline bool is_new() const noexcept { return container.is_new(); }
    inline bool is_local() const noexcept { return container.is_local(); }
    inline bool is_remote() const noexcept { return container.is_remote(); }
    inline bool is_candidate() const noexcept { return container.is_candidate(); }

    folder_presence_t &presence() const {
        auto &tree_item = static_cast<presence_item_t &>(container.container);
        return static_cast<folder_presence_t &>(tree_item.get_presence());
    }

    const model::folder_t &get_folder() const noexcept { return *container.folder; }

    const model::folder_info_t &get_folder_info() const noexcept { return *container.folder_info; }

    const model::cluster_t *get_cluster() const noexcept { return container.container.supervisor.get_cluster(); }

    inline void set_error(std::string_view error) { container.set_error(error); }

    void set_refresh_callback(Fl_Widget *w) {
        w->callback([](auto, void *data) { reinterpret_cast<base_table_t *>(data)->container.refresh(); }, this);
    }

    widgetable_ptr_t make_path(bool disabled) {
        struct widget_t final : table_widget::path_t {
            using parent_t = table_widget::path_t;
            widget_t(Fl_Widget &container, std::string title, bool disabled_)
                : parent_t{container, title}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                input->when(input->when() | FL_WHEN_CHANGED);
                static_cast<base_table_t &>(container).set_refresh_callback(input);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }

            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                auto &path = container.get_folder().get_path();
                input->value(path.get_full_name().data());
            }

            bool store(void *data) override {
                auto ctx = reinterpret_cast<ctx_t *>(data);
                db::set_path(ctx->folder, input->value());
                return true;
            }

            bool disabled;
        };
        return new widget_t(*this, "folder directory", disabled);
    }

    widgetable_ptr_t make_id(bool disabled) {
        struct widget_t final : table_widget::input_t {
            using parent_t = table_widget::input_t;
            widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                static_cast<base_table_t &>(container).set_refresh_callback(input);
                input->when(input->when() | FL_WHEN_CHANGED);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }

            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                auto value = container.get_folder().get_id();
                input->value(value.data());
            }

            bool store(void *data) override {
                auto ctx = reinterpret_cast<ctx_t *>(data);
                db::set_id(ctx->folder, input->value());
                return true;
            }

            bool disabled;
        };
        return new widget_t(*this, disabled);
    }

    widgetable_ptr_t make_label(bool disabled) {
        struct widget_t final : table_widget::input_t {
            using parent_t = table_widget::input_t;
            widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                input->when(input->when() | FL_WHEN_CHANGED);
                static_cast<base_table_t &>(container).set_refresh_callback(input);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }

            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                auto value = container.get_folder().get_label();
                input->value(value.data());
            }

            bool store(void *data) override {
                auto label = std::string_view(input->value());
                if (label.empty()) {
                    auto &container = static_cast<base_table_t &>(this->container);
                    container.set_error("label cannot be empty");
                    return false;
                }

                auto ctx = reinterpret_cast<ctx_t *>(data);
                db::set_label(ctx->folder, label);
                return true;
            }

            bool disabled;
        };
        return new widget_t(*this, disabled);
    }

    widgetable_ptr_t make_pull_order(bool disabled) {
        struct widget_t final : table_widget::choice_t {
            using parent_t = table_widget::choice_t;
            widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                input->size(200, r->h());
                input->when(input->when() | FL_WHEN_CHANGED);
                static_cast<base_table_t &>(container).set_refresh_callback(input);
                input->add("random");
                input->add("alphabetic");
                input->add("smallest first");
                input->add("largest first");
                input->add("oldest first");
                input->add("newest first");
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }

            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                auto value = container.get_folder().get_pull_order();
                input->value(static_cast<int>(value));
            }

            bool store(void *data) override {
                auto ctx = reinterpret_cast<ctx_t *>(data);
                auto value = (db::PullOrder)(input->value());

                db::set_pull_order(ctx->folder, value);
                return true;
            }
            bool disabled;
        };
        return new widget_t(*this, disabled);
    }

    widgetable_ptr_t make_index(bool disabled) {
        struct widget_t final : table_widget::int_input_t {
            using parent_t = table_widget::int_input_t;
            widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                input->when(input->when() | FL_WHEN_CHANGED);
                static_cast<base_table_t &>(container).set_refresh_callback(input);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }

            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                auto value = container.get_folder_info().get_index();
                auto value_str = fmt::format("0x{:x}", value);
                input->value(value_str.data());
            }

            bool store(void *data) override {
                auto ctx = reinterpret_cast<ctx_t *>(data);
                auto value_str = std::string_view(input->value());
                std::uint64_t value = 0;
                auto b = value_str.begin();
                if (value_str.size() >= 2 && value_str.starts_with("0x")) {
                    b += 2;
                }
                auto result = std::from_chars(b, value_str.end(), value, 16);
                if (result.ec != std::errc() || value <= 0) {
                    auto &container = static_cast<base_table_t &>(this->container);
                    container.set_error("invalid index");
                    return false;
                }
                ctx->index = value;
                return true;
            }

            bool disabled;
        };
        return new widget_t(*this, disabled);
    }

    widgetable_ptr_t make_rescan_interval(bool disabled) {
        struct widget_t final : table_widget::int_input_t {
            using parent_t = table_widget::int_input_t;
            widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                input->when(input->when() | FL_WHEN_CHANGED);
                static_cast<base_table_t &>(container).set_refresh_callback(input);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }

            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                auto value = container.get_folder().get_rescan_interval();
                auto value_str = std::to_string(value);
                input->value(value_str.data());
            }

            bool store(void *data) override {
                auto ctx = reinterpret_cast<ctx_t *>(data);
                auto value_str = std::string_view(input->value());
                int value = 0;
                auto result = std::from_chars(value_str.begin(), value_str.end(), value);
                if (result.ec != std::errc() || value <= 0) {
                    auto &container = static_cast<base_table_t &>(this->container);
                    container.set_error("invalid rescan interval");
                    return false;
                }

                auto ri = static_cast<std::uint32_t>(value);
                db::set_rescan_interval(ctx->folder, ri);
                return true;
            }
            bool disabled;
        };
        return new widget_t(*this, disabled);
    }

    widgetable_ptr_t make_watched(bool disabled) {
        struct widget_t final : checkbox_widget_t {
            using parent_t = checkbox_widget_t;
            widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }
            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                input->value(container.get_folder().is_watched());
            }
            bool store(void *data) override {
                auto ctx = reinterpret_cast<ctx_t *>(data);
                db::set_watched(ctx->folder, input->value());
                return true;
            }
            bool disabled;
        };
        return new widget_t(*this, disabled);
    }

    widgetable_ptr_t make_notice() {
        struct widget_t final : table_widget::label_t {
            using parent_t = table_widget::label_t;
            using parent_t::parent_t;

            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                input->label(container.container.error.c_str());
            }
        };
        return new widget_t(*this);
    }

    widgetable_ptr_t make_ignore_permissions(bool disabled) {
        struct widget_t final : checkbox_widget_t {
            using parent_t = checkbox_widget_t;
            widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }
            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                input->value(container.get_folder().are_permissions_ignored());
            }

            bool store(void *data) override {
                auto ctx = reinterpret_cast<ctx_t *>(data);
                db::set_ignore_permissions(ctx->folder, input->value());
                return true;
            }
            bool disabled;
        };
        return new widget_t(*this, disabled);
    }

    widgetable_ptr_t make_ignore_delete(bool disabled) {
        struct widget_t final : checkbox_widget_t {
            using parent_t = checkbox_widget_t;
            widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }
            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                input->value(container.get_folder().is_deletion_ignored());
            }
            bool store(void *data) override {
                auto ctx = reinterpret_cast<ctx_t *>(data);
                db::set_ignore_delete(ctx->folder, input->value());
                return true;
            }
            bool disabled;
        };
        return new widget_t(*this, disabled);
    }

    widgetable_ptr_t make_disable_tmp() {
        struct widget_t final : checkbox_widget_t {
            using parent_t = checkbox_widget_t;
            widget_t(Fl_Widget &container) : parent_t{container}, disabled{true} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }
            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                input->value(true);
            }
            bool store(void *data) override {
                auto ctx = reinterpret_cast<ctx_t *>(data);
                db::set_disable_temp_indexes(ctx->folder, input->value());
                return true;
            }
            bool disabled;
        };
        return new widget_t(*this);
    }

    widgetable_ptr_t make_scheduled(bool disabled) {
        struct widget_t final : checkbox_widget_t {
            using parent_t = checkbox_widget_t;
            widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }
            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                input->value(container.get_folder().is_scheduled());
            }
            bool store(void *data) override {
                auto ctx = reinterpret_cast<ctx_t *>(data);
                db::set_scheduled(ctx->folder, input->value());
                return true;
            }
            bool disabled;
        };
        return new widget_t(*this, disabled);
    }

    widgetable_ptr_t make_paused(bool disabled) {
        struct widget_t final : checkbox_widget_t {
            using parent_t = checkbox_widget_t;
            widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }
            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                input->value(container.get_folder().is_paused());
            }
            bool store(void *data) override {
                auto ctx = reinterpret_cast<ctx_t *>(data);
                db::set_paused(ctx->folder, input->value());
                return true;
            }
            bool disabled;
        };
        return new widget_t(*this, disabled);
    }

    widgetable_ptr_t make_shared_with(model::device_ptr_t device) {
        ++share_widgets;
        return new device_share_widget_t(*this, device);
    }

    void refresh() override {
        parent_t::refresh();

        if (is_local() || is_candidate() || is_new()) {
            auto &folder = get_folder();
            if (scan_start_cell && scan_finish_cell) {
                auto &date_start = folder.get_scan_start();
                auto &date_finish = folder.get_scan_finish();
                auto scan_start = date_start.is_not_a_date_time() ? "-" : model::pt::to_simple_string(date_start);
                auto scan_finish = date_finish.is_not_a_date_time() ? "-" : model::pt::to_simple_string(date_finish);
                scan_start_cell->update(scan_start);
                scan_finish_cell->update(scan_finish);
            }
        }

        if ((is_local() || is_remote()) && entries_cell) {
            auto max_sequence = get_folder_info().get_max_sequence();
            auto &stats = presence().get_stats();
            entries_cell->update(fmt::format("{}", stats.entities));
            entries_size_cell->update(get_file_size(stats.size));
            max_sequence_cell->update(fmt::format("{}", max_sequence));
        }
    }

    void on_add_share(widgetable_t &widget) {
        auto [from_index, count] = scan(widget);
        auto devices_amount = static_cast<int>(get_cluster()->get_devices().size());
        if (count < devices_amount - 1) {
            assert(from_index);
            auto w = widgetable_ptr_t{};
            w.reset(new device_share_widget_t(*this, {}));
            insert_row("shared with", w, from_index + 1);
            refresh();
        }
    }

    bool on_remove_share(widgetable_t &widget, model::device_ptr_t device, model::device_ptr_t initial) {
        bool removed = false;
        auto [_, count] = scan(widget);
        if (count > 1 && !initial) {
            parent_t::remove_row(widget);
            removed = true;
        } else {
            redraw();
        }

        if (device) {
            container.shared_with.remove(device);
            container.non_shared_with.put(device);
        }

        container.refresh();
        return removed;
    }

    void on_select_share(model::device_ptr_t device, model::device_ptr_t previous) {
        if (previous) {
            container.shared_with.remove(previous);
            container.non_shared_with.put(previous);
        }
        if (device) {
            container.shared_with.put(device);
            container.non_shared_with.remove(device);
        }
        container.refresh();
    }

    std::pair<int, int> scan(widgetable_t &widget) {
        auto &rows = get_rows();
        auto from_index = int{-1};
        int count = 0;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            auto item = std::get_if<widgetable_ptr_t>(&rows[i].value);
            if (!item) {
                continue;
            }
            if (!dynamic_cast<device_share_widget_t *>(item->get())) {
                continue;
            }
            ++count;
            if (item->get() == &widget) {
                from_index = i;
            }
        }
        return {from_index, count};
    }

  protected:
    static_string_provider_ptr_t entries_cell;
    static_string_provider_ptr_t entries_size_cell;
    static_string_provider_ptr_t max_sequence_cell;
    static_string_provider_ptr_t scan_start_cell;
    static_string_provider_ptr_t scan_finish_cell;
    int share_widgets{0};
    folder_widget_t &container;
};

device_share_widget_t::device_share_widget_t(Fl_Widget &container, model::device_ptr_t device_)
    : parent_t(container), initial_device{device_}, device{device_}, input{nullptr} {
    auto table = static_cast<base_table_t *>(&container);
    disabled = device && table->scan(*this).first == 0;
}

Fl_Widget *device_share_widget_t::create_widget(int x, int y, int w, int h) {
    auto group = new Fl_Group(x, y, w, h);
    group->begin();
    group->box(FL_FLAT_BOX);
    auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;
    ww = std::min(300, ww);

    input = new Fl_Choice(x + padding, yy, ww, hh);
    auto add = new Fl_Button(input->x() + input->w() + padding * 2, yy, hh, hh, "@+");
    auto remove = new Fl_Button(add->x() + add->w() + padding * 2, yy, hh, hh, "@undo");

    add->callback(
        [](auto, void *data) {
            auto self = reinterpret_cast<device_share_widget_t *>(data);
            auto table = static_cast<base_table_t *>(&self->container);
            table->on_add_share(*self);
        },
        this);
    remove->callback(
        [](auto, void *data) {
            auto self = reinterpret_cast<device_share_widget_t *>(data);
            auto table = static_cast<base_table_t *>(&self->container);
            self->device = {};
            bool ok = table->on_remove_share(*self, self->device, self->initial_device);
            if (!ok) {
                self->input->value(0);
            }
        },
        this);
    input->callback(
        [](auto, void *data) {
            auto self = reinterpret_cast<device_share_widget_t *>(data);
            auto table = static_cast<base_table_t *>(&self->container);
            auto previous = self->device;
            if (self->input->value()) {
                auto cluster = table->get_cluster();
                for (auto &it : cluster->get_devices()) {
                    auto device = it.item.get();
                    if (device == cluster->get_device().get()) {
                        continue;
                    }
                    auto short_id = device->device_id().get_short();
                    auto label = fmt::format("{}, {}", device->get_name(), short_id);
                    if (label == self->input->text()) {
                        self->device = it.item;
                        break;
                    }
                }
            } else {
                self->device = {};
            }
            table->on_select_share(self->device, previous);
        },
        this);

    group->end();
    group->resizable(nullptr);
    widget = group;
    reset();
    return widget;
}

void device_share_widget_t::reset() {
    auto &table = static_cast<base_table_t &>(this->container);
    auto cluster = table.get_cluster();

    input->add("(empty)");
    int i = 1;
    int index = i;
    for (auto &it : cluster->get_devices()) {
        auto &device = it.item;
        if (device == cluster->get_device()) {
            continue;
        }
        auto short_id = device->device_id().get_short();
        auto label = fmt::format("{}, {}", device->get_name(), short_id);
        input->add(label.data());
        if (device.get() == initial_device.get()) {
            this->device = device;
            index = i;
        }
        ++i;
    }
    input->value(this->device ? index : 0);
    if (disabled) {
        widget->deactivate();
    }
}

bool device_share_widget_t::store(void *data) {
    if (device) {
        auto ctx = reinterpret_cast<ctx_t *>(data);
        ctx->shared_with.put(device);
    }
    return true;
}

struct details_table_t final : base_table_t {
    using parent_t = base_table_t;

    details_table_t(folder_widget_t &container_, int x, int y, int w, int h) : parent_t(container_, x, y, w, h) {
        auto data = table_rows_t();
        auto local_or_candidate_or_remote = is_local() || is_candidate() || is_remote();
        auto local_or_remote = is_local() || is_remote();

        data.push_back({"local path", make_path(local_or_remote)});
        data.push_back({"id", make_id(local_or_candidate_or_remote)});
        data.push_back({"label", make_label(is_remote())});
        data.push_back({"pull order", make_pull_order(is_remote())});

        if (local_or_remote) {
            entries_cell = new static_string_provider_t();
            entries_size_cell = new static_string_provider_t();
            max_sequence_cell = new static_string_provider_t();

            data.push_back({"cluster/local entries", entries_cell});
            data.push_back({"entries size", entries_size_cell});
            data.push_back({"max sequence", max_sequence_cell});
        }

        data.push_back({"index", make_index(local_or_candidate_or_remote)});
        data.push_back({"rescan interval", make_rescan_interval(is_remote())});
        if (is_local()) {
            scan_start_cell = new static_string_provider_t();
            scan_finish_cell = new static_string_provider_t();

            data.push_back({"scan start", scan_start_cell});
            data.push_back({"scan finish", scan_finish_cell});
        }
        data.push_back({"watched", make_watched(is_remote())});
        assign_rows(std::move(data));
    }
};

struct tab_content_group : refresheable_group_t {
    using parent_t = refresheable_group_t;
    tab_content_group(int x, int y, int w, int h, const char *label) : parent_t(x, y, w, h, label) { box(FL_FLAT_BOX); }
};

struct sharing_table_t final : base_table_t {
    using parent_t = base_table_t;
    sharing_table_t(folder_widget_t &container_, int x, int y, int w, int h) : parent_t(container_, x, y, w, h) {
        auto data = table_rows_t();

        data.push_back({"ignore permissions", make_ignore_permissions(false)});
        data.push_back({"ignore delete", make_ignore_delete(false)});
        data.push_back({"disable temp indixes", make_disable_tmp()});
        data.push_back({"scheduled", make_scheduled(false)});
        data.push_back({"paused", make_paused(false)});

        int shared_count = 0;
        for (auto it : container_.shared_with) {
            auto &device = it.item;
            auto widget = make_shared_with(device);
            data.push_back({"shared_with", widget});
            ++shared_count;
        }
        if (!shared_count) {
            auto widget = make_shared_with({});
            data.push_back({"shared_with", widget});
        }

        assign_rows(std::move(data));
    }
};

Fl_Widget *checkbox_widget_t::create_widget(int x, int y, int w, int h) {
    auto r = parent_t::create_widget(x, y, w, h);
    static_cast<base_table_t &>(container).set_refresh_callback(input);
    return r;
}

struct tabs_container_t final : contentable_t<Fl_Tabs> {
    using parent_t = contentable_t<Fl_Tabs>;

    tabs_container_t(folder_widget_t &container_, int x, int y, int w, int h) noexcept
        : parent_t(x, y, w, h), container{container_} {
        fl_open_display();
        int tx, ty, tw, th;
        client_area(tx, ty, tw, th);
        begin();
        new refresheable_group_t(tx, ty, tw, th, "");
        end();
    }

    void make_tabs(model::folder_ptr_t f, model::folder_info_ptr_t fi) {
        clear();
        fl_open_display();
        int tx, ty, tw, th;
        client_area(tx, ty, tw, th);
        begin();
        auto &group = make_details_tab(tx, ty, tw, th);
        if (container.behavior != folder_widget_t::behavior_t::remote) {
            make_sharing_tab(tx, ty, tw, th);
        }
        make_file_patterns_tab(tx, ty, tw, th);
        end();
        resizable(group);
    }

    Fl_Widget &make_details_tab(int x, int y, int w, int h) {
        auto *group = new tab_content_group(x, y, w, h, "Details");
        group->begin();
        new details_table_t(container, x, y, w, h);
        group->end();
        return *group;
    }

    Fl_Widget &make_sharing_tab(int x, int y, int w, int h) {
        auto *group = new tab_content_group(x, y, w, h, "Sharing");
        group->begin();
        new sharing_table_t(container, x, y, w, h);
        group->end();
        return *group;
    }

    Fl_Widget &make_file_patterns_tab(int x, int y, int w, int h) {
        auto *group = new tab_content_group(x, y, w, h, "File patterns");
        group->begin();
        auto widget = new file_matching_widget_t(container, x, y, w, h);
        group->resizable(widget);
        group->end();
        return *group;
    }

    folder_widget_t &container;
};

struct button_group_t final : refresheable_group_t {
    using parent_t = refresheable_group_t;

    button_group_t(folder_widget_t &container_, int x, int y, int w, int h) noexcept
        : parent_t(x, y, w, h), container{container_} {
        begin();
        box(FL_FLAT_BOX);
        auto hh = (h - padding * 6) / 2;

        notice = new Fl_Box(x + padding * 2, y + padding * 2, w - padding * 4, hh, "");
        notice->box(FL_FLAT_BOX);
        notice->color(FL_LIGHT2);

        auto xx = x;
        auto yy = notice->y() + notice->h() + padding * 2;
        auto ww = 80;

        auto mask = buttons_mask_t(0);
        if (container.is_new()) {
            mask = B_CREATE;
        }
        if (container.is_local()) {
            mask = B_APPLY | B_REMOVE | B_RESCAN;
        }
        if (container.is_candidate()) {
            mask = B_SHARE;
        }
        if (mask) {
            mask = mask | B_RESET;
        }

        if (mask & B_CREATE) {
            auto button = new Fl_Button(xx, yy, ww, hh, "create");
            button->deactivate();
            button->callback([](auto, void *data) { static_cast<folder_widget_t *>(data)->on_create(); }, &container);
            create_button = button;
        }
        if (mask & B_APPLY) {
            auto button = new Fl_Button(xx, yy, ww, hh, "apply");
            button->deactivate();
            button->callback([](auto, void *data) { static_cast<folder_widget_t *>(data)->on_apply(); }, &container);
            apply_button = button;
        }
        if (mask & B_SHARE) {
            auto button = new Fl_Button(xx, yy, ww, hh, "share");
            button->deactivate();
            button->callback([](auto, void *data) { static_cast<folder_widget_t *>(data)->on_share(); }, &container);
            share_button = button;
        }
        if (mask & B_RESCAN) {
            auto button = new Fl_Button(xx, yy, ww, hh, "rescan");
            button->deactivate();
            button->callback([](auto, void *data) { static_cast<folder_widget_t *>(data)->on_rescan(); }, &container);
            rescan_button = button;
        }
        if (mask & B_RESET) {
            auto button = new Fl_Button(xx, yy, ww, hh, "reset");
            button->deactivate();
            button->callback([](auto, void *data) { static_cast<folder_widget_t *>(data)->on_reset(); }, &container);
            reset_button = button;
        }
        if (mask & B_REMOVE) {
            auto button = new Fl_Button(xx, yy, ww, hh, "remove");
            button->color(FL_RED);
            button->deactivate();
            button->callback([](auto, void *data) { static_cast<folder_widget_t *>(data)->on_remove(); }, &container);
            remove_button = button;
        }

        end();
        resizable(nullptr);
        relayout();
    }

    void resize(int X, int Y, int W, int H) override {
        if (children() > 1) {
            auto children_w = int{0};
            for (int i = 1; i < children(); ++i) {
                auto c = child(i);
                children_w += c->w();
            }
            children_w += children() * (padding * 2);
            W = std::max(children_w, W);
        }
        parent_t::resize(X, Y, W, H);
        relayout();
    }

    void relayout() noexcept {
        notice->resize(x() + padding * 2, y() + padding * 2, w() - padding * 4, notice->h());

        if (children() > 1) {
            auto children_w = int{0};
            for (int i = 1; i < children(); ++i) {
                auto c = child(i);
                children_w += c->w();
            }
            children_w += (children() + 1) * (padding * 2);

            auto xx = x() + (w() / 2) - (children_w / 2) + (padding * 2);
            for (int i = 1; i < children(); ++i) {
                auto c = child(i);
                c->resize(xx, c->y(), c->w(), c->h());
                xx += (padding * 2) + c->w();
            }
        }
    }

    void refresh() override {
        parent_t::refresh();

        auto actions = buttons_mask_t{0};
        if (container.is_local() || container.is_candidate() || container.is_new()) {
            auto &folder = *container.folder;

            ctx_t ctx;
            folder.serialize(ctx.folder);
            auto copy_data = db::encode(ctx.folder);
            auto valid = container.store(&ctx);
            auto copy_data_2 = db::encode(ctx.folder);
            auto fields_are_same = copy_data == copy_data_2;
            auto is_same = fields_are_same && (ctx.shared_with == container.shared_with_orig);

            if (!is_same) {
                if (valid) {
                    actions = actions | B_APPLY | B_CREATE | B_SHARE;
                }
            } else {
                if (valid) {
                    actions = actions | B_RESCAN | B_REMOVE;
                }
            }
            if (!is_same || !valid) {
                actions = actions | B_RESET;
            }
        }

        ENABLE_ACTION(create_button, B_CREATE);
        ENABLE_ACTION(apply_button, B_APPLY);
        ENABLE_ACTION(share_button, B_SHARE);
        ENABLE_ACTION(reset_button, B_RESET);
        ENABLE_ACTION(rescan_button, B_RESCAN);
        ENABLE_ACTION(remove_button, B_REMOVE);

        if (notice) {
            notice->label(container.error.data());
        }
        redraw();
    }

    folder_widget_t &container;
    Fl_Box *notice{nullptr};
    Fl_Widget *create_button{nullptr};
    Fl_Widget *apply_button{nullptr};
    Fl_Widget *share_button{nullptr};
    Fl_Widget *reset_button{nullptr};
    Fl_Widget *rescan_button{nullptr};
    Fl_Widget *remove_button{nullptr};
};

} // namespace

folder_widget_t::folder_widget_t(tree_item_t &container_, behavior_t behavior_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), container{container_}, behavior{behavior_} {
    begin();
    box(FL_FLAT_BOX);
    tabs_container = new tabs_container_t(*this, x, y, w, h - 50);
    new button_group_t(*this, x, tabs_container->y() + tabs_container->h(), w, 50);
    end();
}

void folder_widget_t::refresh() {
    error.clear();
    parent_t::refresh();
}

void folder_widget_t::make_tabs(model::folder_ptr_t f, model::folder_info_ptr_t fi) {
    folder_orig = std::move(f);
    folder_info_orig = std::move(fi);

    sync_shares_with_model();
    reset_data();

    auto tc = static_cast<tabs_container_t *>(tabs_container);
    tc->make_tabs(folder, folder_info);
    resizable(tabs_container);
}

void folder_widget_t::sync_shares_with_model() noexcept {
    shared_with_orig = {};
    non_shared_with_orig = {};

    auto cluster = container.supervisor.get_cluster();
    auto self = cluster->get_device().get();
    for (auto it : cluster->get_devices()) {
        auto &peer = *it.item.get();
        if (&peer != self) {
            if (folder_orig->is_shared_with(peer)) {
                shared_with_orig.put(&peer);
            } else {
                non_shared_with_orig.put(&peer);
            }
        }
    }
    if (auto peer = folder_info_orig->get_device(); peer && peer != self) {
        shared_with_orig.put(peer);
    }
}

void folder_widget_t::reset_data() {
    folder = [this]() -> model::folder_ptr_t {
        auto db = db::Folder();
        folder_orig->serialize(db);
        auto key = folder_orig->get_key();
        auto f = model::folder_t::create(key, db).assume_value();
        f->assign_cluster(folder_orig->get_cluster());
        return f;
    }();

    folder_info = [this]() -> model::folder_info_ptr_t {
        auto db = db::FolderInfo();
        folder_info_orig->serialize(db);
        auto key = folder_info_orig->get_key();
        auto device = folder_info_orig->get_device();
        auto r = model::folder_info_t::create(key, db, device, folder);
        return r.assume_value();
    }();
    shared_with = shared_with_orig;
    non_shared_with = non_shared_with_orig;
}

void folder_widget_t::on_apply() noexcept { create_or_update(); }

void folder_widget_t::on_create() noexcept { create_or_update(); }

void folder_widget_t::on_share() noexcept {
    serialization_context_t ctx;
    auto valid = store(&ctx);
    if (!valid) {
        return;
    }

    auto &folder = ctx.folder;
    auto &sup = container.supervisor;
    auto cluster = sup.get_cluster();
    auto log = sup.get_logger();
    auto label = db::get_label(folder);
    auto folder_id = db::get_id(folder);
    auto cb_ui_select = sup.call_select_folder(folder_id);

    auto existing_folder = sup.get_cluster()->get_folders().by_id(folder_id);
    if (!existing_folder) {
        auto devices = std::vector<utils::bytes_t>{};
        for (auto it : shared_with) {
            auto &device = it.item;
            auto sha256 = device->device_id().get_sha256();
            if (ctx.shared_with.by_sha256(sha256)) {
                devices.emplace_back(utils::bytes_t(sha256.begin(), sha256.end()));
            }
        }

        log->info("going to create folder {}({}) & share it with {} devices", label, folder_id, devices.size());
        auto opt = modify::upsert_folder_t::create(*sup.get_cluster(), sup.get_sequencer(), folder, 0);
        if (!opt) {
            log->error("cannot create folder: {}", opt.assume_error().message());
            return;
        }

        auto cb_share = sup.call_share_folders(folder_id, std::move(devices), cb_ui_select.get());
        sup.send_model<model::payload::model_update_t>(opt.assume_value(), cb_share.get());
    } else {
        log->info("going to share folder {}({}) with {} devices", label, folder_id, shared_with.size());
        auto &sequncecer = sup.get_sequencer();
        auto assember = model::diff::diff_assember_t(constants::diffs_batch);
        using diff_t = model::diff::modify::share_folder_t;
        auto &self = *cluster->get_device();
        for (auto it : shared_with) {
            auto &peer = it.item;
            auto opt = diff_t::create(*cluster, sequncecer, *peer, self.device_id(), *existing_folder);
            if (!opt) {
                auto message = opt.assume_error().message();
                log->error("cannot share folder {} with {} : {}", folder_id, peer->device_id(), message);
            } else {
                assember.push_back(opt.assume_value().get());
            }
        }
        if (auto diff = assember.consume(); diff) {
            sup.send_model<model::payload::model_update_t>(diff, cb_ui_select.get());
        }
    }
}

void folder_widget_t::on_rescan() noexcept {
    auto &sup = container.supervisor;
    auto diff = model::diff::cluster_diff_ptr_t{};
    auto folder_id = folder->get_id();
    diff = new model::diff::local::scan_request_t(folder_id, {});
    sup.send_model<model::payload::model_update_t>(std::move(diff), this);
}

void folder_widget_t::on_remove() noexcept {
    auto r = fl_choice("Are you sure? (no files on disk are touched)", "Yes", "No", nullptr);
    if (r != 0) {
        return;
    }
    auto &sup = container.supervisor;
    auto &cluster = *sup.get_cluster();
    auto &sequencer = sup.get_sequencer();
    auto &f = *folder_orig;
    auto diff = cluster_diff_ptr_t{};
    auto cb_reset = callback_ptr_t();
    cb_reset = new callback_t([this]() {
        folder.reset();
        folder_orig.reset();
        folder_info.reset();
        folder_info_orig.reset();
    });
    container.supervisor.add_callback(cb_reset);
    diff = new modify::suspend_folder_t(f, true);
    diff->assign_sibling(new modify::remove_folder_t(cluster, sequencer, f));
    sup.send_model<model::payload::model_update_t>(std::move(diff), cb_reset.get());
}

void folder_widget_t::on_reset() noexcept {
    reset_data();
    reset();
    refresh();
}

void folder_widget_t::create_or_update() noexcept {
    serialization_context_t ctx;
    auto valid = store(&ctx);
    if (!valid) {
        return;
    }

    auto &sup = container.supervisor;
    auto &folder_db = ctx.folder;
    auto log = sup.get_logger();
    auto &cluster = *sup.get_cluster();

    auto opt = modify::upsert_folder_t::create(*sup.get_cluster(), sup.get_sequencer(), folder_db, ctx.index);
    if (!opt) {
        log->error("cannot create folder: {}", opt.assume_error().message());
        return;
    }
    auto assember = model::diff::diff_assember_t(constants::diffs_batch);
    assember.push_back(opt.assume_value().get());

    if (shared_with_orig.size()) {
        auto folder = cluster.get_folders().by_id(folder_orig->get_id());
        auto orphaned_blocks = model::orphaned_blocks_t{};
        auto &folder_infos = folder->get_folder_infos();
        for (auto it : shared_with_orig) {
            auto &device = it.item;
            if (!ctx.shared_with.by_sha256(device->device_id().get_sha256())) {
                auto folder_info = folder_infos.by_device(*device);
                if (folder_info) {
                    log->info("going to unshare folder '{}' with {}({})", folder->get_label(), device->get_name(),
                              device->device_id().get_short());
                    auto sub_diff = model::diff::cluster_diff_ptr_t{};
                    assember.push_back(new modify::unshare_folder_t(cluster, *folder_info, &orphaned_blocks));
                }
            }
        }
        if (auto orphaned_set = orphaned_blocks.deduce(); orphaned_set.size()) {
            log->info("going to remove {} orphaned blocks", orphaned_set.size());
            auto sub_diff = model::diff::cluster_diff_ptr_t{};
            assember.push_back(new modify::remove_blocks_t(std::move(orphaned_set)));
        }
    }

    auto devices = std::vector<utils::bytes_t>{};
    for (auto it : non_shared_with_orig) {
        auto &device = it.item;
        auto sha256 = device->device_id().get_sha256();
        if (ctx.shared_with.by_sha256(sha256)) {
            devices.emplace_back(utils::bytes_t(sha256.begin(), sha256.end()));
        }
    }

    auto folder_id = db::get_id(folder_db);
    auto ui_next = callback_ptr_t();
    auto cb_ui_refresh = callback_ptr_t();
    if (behavior == behavior_t::edit_new || behavior == behavior_t::candiate) {
        cb_ui_refresh = sup.call_select_folder(folder_id);
    } else if (behavior == behavior_t::local) {
        cb_ui_refresh = new callback_t([this]() {
            sync_shares_with_model();
            reset_data();
            refresh();
        });
        container.supervisor.add_callback(cb_ui_refresh);
    }
    if (devices.empty()) {
        ui_next = cb_ui_refresh;
    } else {
        ui_next = sup.call_share_folders(folder_id, std::move(devices), cb_ui_refresh.get());
    }
    sup.send_model<model::payload::model_update_t>(assember.consume(), ui_next.get());
}

void folder_widget_t::set_error(std::string_view error_) noexcept { error = error_; }
