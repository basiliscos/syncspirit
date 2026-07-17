// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "folder_widget.h"
#include "proto/proto-helpers-db.h"
#include "presentation/folder_presence.h"

#include "static_table.h"
#include "presence_item.h"
#include "table_widget/checkbox.h"
#include "table_widget/choice.h"
#include "table_widget/input.h"
#include "table_widget/int_input.h"
#include "table_widget/label.h"
#include "table_widget/path.h"
#include "utils.hpp"

#include <FL/platform.H>

using namespace syncspirit;
using namespace syncspirit::presentation;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::content;

namespace {

#define ENABLE_ACTION(B, FLAG)  \
    if (B) {                    \
        if (actions & FLAG) {   \
            B->activate();      \
        } else {                \
            B->deactivate();    \
        }                       \
    }

static constexpr int padding = 2;

using buttons_mask_t = std::uint32_t;

static auto constexpr B_CREATE = buttons_mask_t{1 << 0};
static auto constexpr B_APPLY = buttons_mask_t{1 << 1};
static auto constexpr B_SHARE = buttons_mask_t{1 << 2};
static auto constexpr B_RESET = buttons_mask_t{1 << 3};
static auto constexpr B_RESCAN = buttons_mask_t{1 << 4};
static auto constexpr B_REMOVE = buttons_mask_t{1 << 5};

struct serialization_context_t {
    db::Folder folder;
    std::uint64_t index;
    model::devices_map_t shared_with;
};

using ctx_t = serialization_context_t;

struct base_table_t;

struct checkbox_widget_t : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;
    using parent_t::parent_t;

    Fl_Widget *create_widget(int x, int y, int w, int h) override;
};

struct base_table_t : syncspirit::fltk::static_table_t {
    using parent_t = syncspirit::fltk::static_table_t;
    using B = content::folder_widget_t::behavior_t;

    base_table_t(folder_widget_t& container_, int x, int y, int w, int h):
        parent_t(x,y,w,h), container{container_} {
    }


    inline bool is_new() const noexcept { return container.behavior == B::edit_new; }
    inline bool is_local() const noexcept { return container.behavior == B::local; }
    inline bool is_remote() const noexcept { return container.behavior == B::remote; }
    inline bool is_candidate() const noexcept { return container.behavior == B::candiate; }

    folder_presence_t& presence() const {
        auto& tree_item = static_cast<presence_item_t &>(container.container);
        return static_cast<folder_presence_t &>(tree_item.get_presence());
    }

    const model::folder_t& get_folder() const noexcept {
        return *container.folder;
    }

    const model::folder_info_t& get_folder_info() const noexcept {
        return *container.folder_info;
    }

    inline void set_error(std::string_view error) {
        container.error = error;
    }

    widgetable_ptr_t make_path(bool disabled) {
        struct widget_t final : table_widget::path_t {
            using parent_t = table_widget::path_t;
            widget_t(Fl_Widget &container, std::string title, bool disabled_)
                : parent_t{container, title}, disabled{disabled_} {}

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto r = parent_t::create_widget(x, y, w, h);
                input->when(input->when() | FL_WHEN_CHANGED);
                input->callback([](auto, void *data) { reinterpret_cast<static_table_t *>(data)->refresh(); }, &container);
                if (disabled) {
                    widget->deactivate();
                }
                return r;
            }

            void reset() override {
                auto &container = static_cast<base_table_t &>(this->container);
                auto& path = container.get_folder().get_path();
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
                input->callback([](auto, void *data) { reinterpret_cast<base_table_t *>(data)->refresh(); }, &container);
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
                input->callback([](auto, void *data) { reinterpret_cast<base_table_t *>(data)->refresh(); }, &container);
                input->when(input->when() | FL_WHEN_CHANGED);
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
                    auto &container = static_cast<base_table_t&>(this->container);
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
                input->callback([](auto, void *data) { reinterpret_cast<base_table_t *>(data)->refresh(); }, &container);
                input->when(input->when() | FL_WHEN_CHANGED);
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
                input->callback([](auto, void *data) { reinterpret_cast<base_table_t *>(data)->refresh(); }, &container);
                input->when(input->when() | FL_WHEN_CHANGED);
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
                input->callback([](auto, void *data) { reinterpret_cast<base_table_t *>(data)->refresh(); }, &container);
                input->when(input->when() | FL_WHEN_CHANGED);
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

    auto make_actions(buttons_mask_t mask) -> widgetable_ptr_t {
        struct widget_t final : widgetable_t {
            using parent_t = widgetable_t;
            widget_t(Fl_Widget &container, buttons_mask_t mask_): parent_t{container}, mask{mask_} {
            }

            Fl_Widget *create_widget(int x, int y, int w, int h) override {
                auto group = new Fl_Group(x, y, w, h);
                group->begin();
                group->box(FL_FLAT_BOX);
                auto &container = static_cast<base_table_t &>(this->container);
                auto xx = x + padding, yy = y + padding, ww = 100, hh = h - padding * 2;

                if (mask & B_APPLY) {
                    auto apply = new Fl_Button(xx, yy, ww, hh, "apply");
                    apply->deactivate();
                    apply->callback([](auto, void *data) { static_cast<base_table_t *>(data)->on_apply(); }, &container);
                    container.apply_button = apply;
                    xx = apply->x() + ww + padding * 2;
                }

                if (mask & B_CREATE) {
                    auto button = new Fl_Button(xx, yy, ww, hh, "create");
                    button->deactivate();
                    button->callback([](auto, void *data) { static_cast<base_table_t *>(data)->on_create(); }, &container);
                    container.create_button = button;
                    xx = button->x() + ww + padding * 2;
                }

                if (mask & B_SHARE) {
                    auto button = new Fl_Button(xx, yy, ww, hh, "share");
                    button->deactivate();
                    button->callback([](auto, void *data) { static_cast<base_table_t *>(data)->on_share(); }, &container);
                    container.share_button = button;
                    xx = button->x() + ww + padding * 2;
                }

                if (mask & B_RESET) {
                    auto reset = new Fl_Button(xx, yy, ww, hh, "reset");
                    reset->deactivate();
                    reset->callback([](auto, void *data) { static_cast<base_table_t *>(data)->on_reset(); }, &container);
                    container.reset_button = reset;
                    xx = reset->x() + ww + padding * 2;
                }

                if (mask & B_RESCAN) {
                    auto rescan = new Fl_Button(xx, yy, ww, hh, "rescan");
                    rescan->callback([](auto, void *data) { static_cast<base_table_t *>(data)->on_rescan(); }, &container);
                    rescan->deactivate();
                    container.rescan_button = rescan;
                    xx = rescan->x() + ww + padding * 2;
                }

                if (mask & B_REMOVE) {
                    auto remove = new Fl_Button(xx, yy, ww, hh, "remove");
                    remove->callback([](auto, void *data) { static_cast<base_table_t *>(data)->on_remove(); }, &container);
                    remove->color(FL_RED);
                    container.remove_button = remove;
                    xx = remove->x() + ww + padding * 2;
                }

                group->resizable(nullptr);
                group->end();
                widget = group;

                this->reset();
                return widget;
            }

            buttons_mask_t mask;
        };

        return new widget_t(*this, mask);
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

    void add_actions_and_notice(table_rows_t& data) noexcept {
        data.push_back({"", notice = make_notice()});

        auto actions = buttons_mask_t{0};
        if (is_new()) {
            actions = B_CREATE;
        }
        if (is_local()) {
            actions = B_APPLY | B_REMOVE | B_RESCAN;
        }
        if (is_candidate()) {
            actions = B_SHARE;
        }
        if (actions) {
            actions = actions | B_RESET;
            data.push_back({"actions", make_actions(actions)});
        }
    }

    void refresh() override {
        parent_t::refresh();


        auto actions = buttons_mask_t{0};
        if (is_local() || is_candidate() || is_new()) {
            serialization_context_t ctx;

            get_folder().serialize(ctx.folder);
            auto copy_data = db::encode(ctx.folder);
            set_error({});
            auto valid = store(&ctx);
            auto is_same = copy_data == db::encode(ctx.folder);

            auto& folder = get_folder();

            if (scan_start_cell && scan_finish_cell) {
                auto &date_start = folder.get_scan_start();
                auto &date_finish = folder.get_scan_finish();
                auto scan_start = date_start.is_not_a_date_time() ? "-" : model::pt::to_simple_string(date_start);
                auto scan_finish = date_finish.is_not_a_date_time() ? "-" : model::pt::to_simple_string(date_finish);
                scan_start_cell->update(scan_start);
                scan_finish_cell->update(scan_finish);
            }

            if (!is_same) {
                 if (valid) {
                     actions = actions | B_APPLY;
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

        if ((is_local() || is_remote()) && entries_cell) {
            auto max_sequence = get_folder_info().get_max_sequence();
            auto &stats = presence().get_stats();
            entries_cell->update(fmt::format("{}", stats.entities));
            entries_size_cell->update(get_file_size(stats.size));
            max_sequence_cell->update(fmt::format("{}", max_sequence));
        }

        ENABLE_ACTION(create_button, B_CREATE);
        ENABLE_ACTION(apply_button, B_APPLY);
        ENABLE_ACTION(share_button, B_SHARE);
        ENABLE_ACTION(reset_button, B_RESET);
        ENABLE_ACTION(rescan_button, B_RESCAN);
        ENABLE_ACTION(remove_button, B_REMOVE);

        notice->reset();
    }


    virtual void on_apply() noexcept {}
    virtual void on_create() noexcept {}
    virtual void on_share() noexcept {}

    void on_reset() noexcept {
        container.reset_data();
        reset();
        refresh();
    }
    virtual void on_rescan() noexcept {}
    virtual void on_remove() noexcept {}

protected:
    widgetable_ptr_t notice;
    static_string_provider_ptr_t entries_cell;
    static_string_provider_ptr_t entries_size_cell;
    static_string_provider_ptr_t max_sequence_cell;
    static_string_provider_ptr_t scan_start_cell;
    static_string_provider_ptr_t scan_finish_cell;

    Fl_Widget *create_button{nullptr};
    Fl_Widget *apply_button{nullptr};
    Fl_Widget *share_button{nullptr};
    Fl_Widget *reset_button{nullptr};
    Fl_Widget *rescan_button{nullptr};
    Fl_Widget *remove_button{nullptr};

private:
    folder_widget_t& container;
};

struct details_table_t final : base_table_t {
    using parent_t = base_table_t;

    details_table_t(folder_widget_t& container_, int x, int y, int w, int h)
        : parent_t(container_, x, y, w, h) {
        auto data = table_rows_t();
        auto local_or_candidate_or_remote = is_local() || is_candidate() || is_remote();
        auto local_or_remote= is_local() || is_remote();

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
        add_actions_and_notice(data);
        assign_rows(std::move(data));
    }
};

struct tab_content_group: refresheable_group_t {
    using parent_t = refresheable_group_t;
    tab_content_group(int x, int y, int w, int h, const char* label): parent_t(x, y, w, h, label) {
        box(FL_FLAT_BOX);
    }
};

struct sharing_table_t final : base_table_t {
    using parent_t = base_table_t;
    sharing_table_t(folder_widget_t& container_, int x, int y, int w, int h)
        : parent_t(container_, x, y, w, h) {
        auto data = table_rows_t();

        data.push_back({"ignore permissions", make_ignore_permissions(false)});
        data.push_back({"ignore delete", make_ignore_delete(false)});
        data.push_back({"disable temp indixes", make_disable_tmp()});
        data.push_back({"scheduled", make_scheduled(false)});
        data.push_back({"paused", make_paused(false)});
        add_actions_and_notice(data);
        assign_rows(std::move(data));
    }
};

Fl_Widget *checkbox_widget_t::create_widget(int x, int y, int w, int h) {
   auto r = parent_t::create_widget(x, y, w, h);
   input->callback([](auto, void *data) { reinterpret_cast<base_table_t *>(data)->refresh(); }, &container);
   return r;
}

}

folder_widget_t::folder_widget_t(tree_item_t &container_,
                                 behavior_t behavior_, int x, int y, int w, int h):
    parent_t(x,y,w,h), container{container_}, behavior{behavior_} {
}

void folder_widget_t::make_tabs(model::folder_ptr_t f, model::folder_info_ptr_t fi) {
    folder_info_orig = std::move(fi);
    folder_orig = std::move(f);
    reset_data();
    fl_open_display();
    int tx , ty, tw, th;
    client_area(tx, ty, tw, th);
    begin();
    auto& group = make_details_tab(tx, ty, tw, th);
    if (behavior != behavior_t::remote) {
        make_sharing_tab(tx, ty, tw, th);
    }
    make_file_patterns_tab(tx, ty, tw, th);
    end();
    resizable(group);
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
}


Fl_Widget& folder_widget_t::make_details_tab(int x, int y, int w, int h) {
    auto *group = new tab_content_group(x, y, w, h, "Details");
    group->begin();
    new details_table_t(*this,x,y,w,h);
    group->end();
    return *group;
}

Fl_Widget& folder_widget_t::make_sharing_tab(int x, int y, int w, int h) {
    auto *group = new tab_content_group(x, y, w, h, "Sharing");
    group->begin();
    new sharing_table_t(*this,x,y,w,h);
    group->end();
    return *group;
}

Fl_Widget& folder_widget_t::make_file_patterns_tab(int x, int y, int w, int h) {
    auto *group = new tab_content_group(x, y, w, h, "File patterns");
    group->begin();
    group->color(FL_DARK_MAGENTA);
    group->end();
    return *group;
}
