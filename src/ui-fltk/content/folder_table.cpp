#include "folder_table.h"

#include "model/diff/modify/remove_folder.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/unshare_folder.h"
#include "model/diff/modify/upsert_folder.h"

#include "../table_widget/checkbox.h"
#include "../table_widget/choice.h"
#include "../table_widget/input.h"
#include "../table_widget/int_input.h"
#include "../table_widget/label.h"
#include "../table_widget/path.h"

#include <FL/fl_ask.H>

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::content;

static constexpr int padding = 2;

namespace {

auto static make_path(folder_table_t &container) -> widgetable_ptr_t;
auto static make_id(folder_table_t &container) -> widgetable_ptr_t;
auto static make_label(folder_table_t &container) -> widgetable_ptr_t;
auto static make_folder_type(folder_table_t &container) -> widgetable_ptr_t;
auto static make_pull_order(folder_table_t &container) -> widgetable_ptr_t;
auto static make_index(folder_table_t &container) -> widgetable_ptr_t;
auto static make_read_only(folder_table_t &container) -> widgetable_ptr_t;
auto static make_rescan_interval(folder_table_t &container) -> widgetable_ptr_t;
auto static make_ignore_permissions(folder_table_t &container) -> widgetable_ptr_t;
auto static make_ignore_delete(folder_table_t &container) -> widgetable_ptr_t;
auto static make_disable_tmp(folder_table_t &container) -> widgetable_ptr_t;
auto static make_paused(folder_table_t &container) -> widgetable_ptr_t;
auto static make_shared_with(folder_table_t &container, model::device_ptr_t device) -> widgetable_ptr_t;
auto static make_notice(folder_table_t &container) -> widgetable_ptr_t;
auto static make_actions(folder_table_t &container) -> widgetable_ptr_t;

using ctx_t = folder_table_t::serialiazation_context_t;

struct device_share_widget_t final : widgetable_t {
    using parent_t = widgetable_t;
    device_share_widget_t(folder_table_t &container, model::device_ptr_t device_);

    Fl_Widget *create_widget(int x, int y, int w, int h) override;
    void reset() override;
    bool store(void *data) override;

    model::device_ptr_t initial_device;
    model::device_ptr_t device;
    Fl_Choice *input;
};

device_share_widget_t::device_share_widget_t(folder_table_t &container, model::device_ptr_t device_)
    : parent_t(container), initial_device{device_}, device{device_}, input{nullptr} {}

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
            auto table = static_cast<folder_table_t *>(&self->container);
            table->on_add_share(*self);
        },
        this);
    remove->callback(
        [](auto, void *data) {
            auto self = reinterpret_cast<device_share_widget_t *>(data);
            auto table = static_cast<folder_table_t *>(&self->container);
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
            auto table = static_cast<folder_table_t *>(&self->container);
            auto previous = self->device;
            if (self->input->value()) {
                auto cluster = table->container.supervisor.get_cluster();
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
            table->on_select(self->device, previous);
        },
        this);

    group->end();
    group->resizable(nullptr);
    widget = group;
    reset();
    return widget;
}

void device_share_widget_t::reset() {
    auto &table = static_cast<folder_table_t &>(this->container);
    auto cluster = table.container.supervisor.get_cluster();

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
    if (table.mode == folder_table_t::mode_t::share) {
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

struct checkbox_widget_t : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;
    using parent_t::parent_t;

    Fl_Widget *create_widget(int x, int y, int w, int h) override {
        auto r = parent_t::create_widget(x, y, w, h);
        input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
        return r;
    }
};

auto static make_path(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::path_t {
        using parent_t = table_widget::path_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->when(input->when() | FL_WHEN_CHANGED);
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            auto &container = static_cast<folder_table_t &>(this->container);
            if (container.mode == folder_table_t::mode_t::edit) {
                widget->deactivate();
            }
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto path = container.folder_data.get_path();
            auto value = path.string();
            input->value(value.data());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_path(input->value());
            return true;
        }
    };
    return new widget_t(container, "folder directory");
}

auto static make_id(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::label_t {
        using parent_t = table_widget::label_t;
        using parent_t::parent_t;

        void reset() override {
            auto id = static_cast<folder_table_t &>(container).folder_data.get_id();
            input->label(id.data());
        }
        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_id(input->label());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_label(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::input_t {
        using parent_t = table_widget::input_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto value = container.folder_data.get_label();
            input->value(value.data());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_label(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_folder_type(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::choice_t {
        using parent_t = table_widget::choice_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->size(200, r->h());
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            input->add("Send and Receive");
            input->add("Send only");
            input->add("Receive only");
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto value = container.folder_data.get_folder_type();
            input->value(static_cast<int>(value));
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            auto value = (db::FolderType)(input->value());

            ctx->folder.set_folder_type(value);
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_pull_order(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::choice_t {
        using parent_t = table_widget::choice_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->size(200, r->h());
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            input->add("random");
            input->add("alphabetic");
            input->add("smallest first");
            input->add("largest first");
            input->add("oldest first");
            input->add("newest first");
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto value = container.folder_data.get_pull_order();
            input->value(static_cast<int>(value));
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            auto value = (db::PullOrder)(input->value());

            ctx->folder.set_pull_order(value);
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_index(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::int_input_t {
        using parent_t = table_widget::int_input_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            auto &container = static_cast<folder_table_t &>(this->container);
            if (container.mode != folder_table_t::mode_t::create) {
                widget->deactivate();
            }
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto value_str = std::to_string(container.index);
            input->value(value_str.data());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            auto value_str = std::string_view(input->value());
            int value = 0;
            auto result = std::from_chars(value_str.begin(), value_str.end(), value);
            if (result.ec != std::errc() || value <= 0) {
                auto &container = static_cast<folder_table_t &>(this->container);
                container.error = "invalid index";
                return false;
            }
            ctx->index = static_cast<std::uint32_t>(value);
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_read_only(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            input->value(container.folder_data.is_read_only());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_read_only(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_rescan_interval(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::int_input_t {
        using parent_t = table_widget::int_input_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto value = container.folder_data.get_rescan_interval();
            auto value_str = std::to_string(value);
            input->value(value_str.data());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            auto value_str = std::string_view(input->value());
            int value = 0;
            auto result = std::from_chars(value_str.begin(), value_str.end(), value);
            if (result.ec != std::errc() || value <= 0) {
                auto &container = static_cast<folder_table_t &>(this->container);
                container.error = "invalid rescan interval";
                return false;
            }

            ctx->folder.set_rescan_interval(static_cast<std::uint32_t>(value));
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_ignore_permissions(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            input->value(container.folder_data.are_permissions_ignored());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_ignore_permissions(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_ignore_delete(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            input->value(container.folder_data.is_deletion_ignored());
        }
        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_ignore_delete(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_disable_tmp(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            input->value(container.folder_data.are_temp_indixes_disabled());
        }
        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_disable_temp_indexes(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_paused(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            input->value(container.folder_data.is_paused());
        }
        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_paused(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_shared_with(folder_table_t &container, model::device_ptr_t device) -> widgetable_ptr_t {
    return new device_share_widget_t(container, device);
}

auto static make_notice(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::label_t {
        using parent_t = table_widget::label_t;
        using parent_t::parent_t;

        void reset() override {
            auto &label = static_cast<folder_table_t &>(container).error;
            input->label(label.c_str());
        }
    };
    return new widget_t(container);
}

auto static make_actions(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            using M = folder_table_t::mode_t;
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto &container = static_cast<folder_table_t &>(this->container);

            auto yy = y + padding, ww = 100, hh = h - padding * 2;
            int xx;
            if (container.mode == M::share) {
                auto share = new Fl_Button(x + padding, yy, ww, hh, "share");
                share->deactivate();
                share->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_share(); }, &container);
                xx = share->x() + ww + padding * 2;
                container.share_button = share;
            } else if (container.mode == M::edit) {
                auto apply = new Fl_Button(x + padding, yy, ww, hh, "apply");
                apply->deactivate();
                apply->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_apply(); }, &container);
                container.apply_button = apply;
                xx = apply->x() + ww + padding * 2;
            } else if (container.mode == M::create) {
                auto apply = new Fl_Button(x + padding, yy, ww, hh, "create");
                apply->deactivate();
                apply->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_create(); }, &container);
                container.apply_button = apply;
                xx = apply->x() + ww + padding * 2;
            }
            auto reset = new Fl_Button(xx, yy, ww, hh, "reset");
            reset->deactivate();
            reset->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_reset(); }, &container);
            container.reset_button = reset;
            xx = reset->x() + ww + padding * 2;

            if (container.mode == M::edit) {
                auto rescan = new Fl_Button(xx, yy, ww, hh, "rescan");
                rescan->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_rescan(); },
                                 &container);
                rescan->deactivate();

                auto remove = new Fl_Button(rescan->x() + ww + padding * 2, yy, ww, hh, "remove");
                remove->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_remove(); },
                                 &container);
                remove->color(FL_RED);
                xx = remove->x() + ww + padding * 2;
            }
            auto invisible = new Fl_Box(xx, yy, w - (xx - group->x() + padding * 2), hh);
            invisible->hide();
            group->resizable(invisible);
            group->end();
            widget = group;

            this->reset();
            return widget;
        }
    };

    return new widget_t(container);
}

} // namespace

folder_table_t::folder_table_t(tree_item_t &container_, const folder_description_t &folder_descr, mode_t mode_, int x,
                               int y, int w, int h)
    : parent_t(x, y, w, h), container{container_}, folder_data{folder_descr.folder_data}, mode{mode_},
      entries{folder_descr.entries}, index{folder_descr.index}, max_sequence{folder_descr.max_sequence},
      shared_with{folder_descr.shared_with}, non_shared_with{folder_descr.non_shared_with}, apply_button{nullptr},
      share_button{nullptr}, reset_button{nullptr} {

    auto data = table_rows_t();
    if (mode != mode_t::edit) {
        auto &path = container.supervisor.get_app_config().default_location;
        folder_data.set_path(path);
        folder_data.set_rescan_interval(3600u);
    }

    data.push_back({"path", make_path(*this)});
    data.push_back({"id", make_id(*this)});
    data.push_back({"label", make_label(*this)});
    data.push_back({"type", make_folder_type(*this)});
    data.push_back({"pull order", make_pull_order(*this)});
    data.push_back({"entries", std::to_string(entries)});
    data.push_back({"index", make_index(*this)});
    data.push_back({"max sequence", std::to_string(max_sequence)});
    data.push_back({"read only", make_read_only(*this)});
    data.push_back({"rescan interval", make_rescan_interval(*this)});
    data.push_back({"ignore permissions", make_ignore_permissions(*this)});
    data.push_back({"ignore delete", make_ignore_delete(*this)});
    data.push_back({"disable temp indixes", make_disable_tmp(*this)});
    data.push_back({"paused", make_paused(*this)});

    auto cluster = container.supervisor.get_cluster();
    int shared_count = 0;
    for (auto it : *shared_with) {
        auto &device = it.item;
        auto widget = make_shared_with(*this, device);
        data.push_back({"shared_with", widget});
        ++shared_count;
    }
    if (!shared_count) {
        auto widget = make_shared_with(*this, {});
        data.push_back({"shared_with", widget});
    }
    data.push_back({"", notice = make_notice(*this)});
    data.push_back({"actions", make_actions(*this)});

    initially_shared_with = *shared_with;
    initially_non_shared_with = *non_shared_with;
    assign_rows(std::move(data));
}

bool folder_table_t::on_remove_share(widgetable_t &widget, model::device_ptr_t device, model::device_ptr_t initial) {
    bool removed = false;
    auto [_, count] = scan(widget);
    if (count > 1 && !initial) {
        parent_t::remove_row(widget);
        removed = (bool)device;
    } else {
        redraw();
    }

    if (device) {
        shared_with->remove(device);
        non_shared_with->put(device);
    }
    container.refresh_content();
    return removed;
}

void folder_table_t::on_select(model::device_ptr_t device, model::device_ptr_t previous) {
    if (previous) {
        shared_with->remove(previous);
        non_shared_with->put(previous);
    }
    if (device) {
        shared_with->put(device);
        non_shared_with->remove(device);
    }
    container.refresh_content();
}

void folder_table_t::on_add_share(widgetable_t &widget) {
    auto [from_index, count] = scan(widget);
    if (count < container.supervisor.get_cluster()->get_devices().size() - 1) {
        assert(from_index);
        auto w = widgetable_ptr_t{};
        w.reset(new device_share_widget_t(*this, {}));
        insert_row("shared with", w, from_index + 1);
        refresh();
    }
}

std::pair<int, int> folder_table_t::scan(widgetable_t &widget) {
    auto &rows = get_rows();
    auto from_index = int{-1};
    int count = 0;
    for (int i = 0; i < rows.size(); ++i) {
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

void folder_table_t::refresh() {
    serialiazation_context_t ctx;
    folder_data.serialize(ctx.folder);

    auto copy_data = ctx.folder.SerializeAsString();
    error = {};
    auto valid = store(&ctx);

    // clang-format off
    auto is_same = (copy_data == ctx.folder.SerializeAsString())
                && (initially_shared_with == ctx.shared_with);
    // clang-format on
    if (!is_same) {
        if (valid) {
            if (mode == mode_t::edit) {
                apply_button->activate();
            }
        }
        reset_button->activate();
    } else {
        if (mode == mode_t::edit) {
            apply_button->deactivate();
        }
        reset_button->deactivate();
    }

    if (mode == mode_t::share) {
        if (valid) {
            if (ctx.folder.path().empty()) {
                error = "path should be defined";
            } else {
                auto path = bfs::path(ctx.folder.path());
                auto ec = sys::error_code{};
                if (bfs::exists(path, ec)) {
                    if (!bfs::is_empty(path, ec)) {
                        error = "referred directory should be empty";
                    }
                }
            }
        }

        if (valid && error.empty()) {
            share_button->activate();
        } else {
            share_button->deactivate();
        }
    }
    notice->reset();
}

void folder_table_t::on_share() {
    serialiazation_context_t ctx;
    auto valid = store(&ctx);
    if (!valid) {
        return;
    }

    auto &folder = ctx.folder;
    auto &sup = container.supervisor;
    auto log = sup.get_logger();
    auto &peer = ctx.shared_with.begin()->item;
    log->info("going to create folder {}({}) & share it with {}", folder.label(), folder.id(), peer->get_name());
    auto peer_id = peer->device_id().get_sha256();
    auto opt = modify::upsert_folder_t::create(*sup.get_cluster(), sup.get_sequencer(), folder);
    if (!opt) {
        log->error("cannot create folder: {}", opt.assume_error().message());
        return;
    }

    auto cb = sup.call_share_folder(folder.id(), peer_id);
    sup.send_model<model::payload::model_update_t>(opt.assume_value(), cb.get());
}

void folder_table_t::on_apply() {
    serialiazation_context_t ctx;
    auto valid = store(&ctx);
    if (!valid) {
        return;
    }

    auto &sup = container.supervisor;
    auto log = sup.get_logger();
    auto &cluster = *sup.get_cluster();
    auto folder = cluster.get_folders().by_id(folder_data.get_id());
    auto &folder_infos = folder->get_folder_infos();
    auto diff = model::diff::cluster_diff_ptr_t{};
    auto current = diff.get();
    auto orphaned_blocks = model::orphaned_blocks_t{};

    for (auto it : initially_shared_with) {
        auto &device = it.item;
        if (!ctx.shared_with.by_sha256(device->device_id().get_sha256())) {
            auto folder_info = folder_infos.by_device(*device);
            if (folder_info) {
                log->info("going to unshare folder '{}' with {}({})", folder->get_label(), device->get_name(),
                          device->device_id().get_short());
                auto sub_diff = model::diff::cluster_diff_ptr_t{};
                sub_diff = new modify::unshare_folder_t(cluster, *folder_info, &orphaned_blocks);
                if (diff) {
                    current = current->assign_sibling(sub_diff.get());
                } else {
                    diff = current = sub_diff.get();
                }
            }
        }
    }

    for (auto it : initially_non_shared_with) {
        auto &device = it.item;
        if (ctx.shared_with.by_sha256(device->device_id().get_sha256())) {
            auto opt = modify::share_folder_t::create(cluster, sup.get_sequencer(), *device, *folder);
            if (!opt) {
                log->error("folder cannot be sahred: {}", opt.assume_error().message());
                return;
            }
            log->info("going to share folder '{}' with {}({})", folder->get_label(), device->get_name(),
                      device->device_id().get_short());
            auto ptr = opt.assume_value().get();
            if (diff) {
                current = current->assign_sibling(ptr);
            } else {
                diff = current = ptr;
            }
        }
    }

    if (auto orphaned_set = orphaned_blocks.deduce(); orphaned_set.size()) {
        log->info("going to remove {} orphaned blocks", orphaned_set.size());
        auto sub_diff = model::diff::cluster_diff_ptr_t{};
        sub_diff = new modify::remove_blocks_t(std::move(orphaned_set));
        current = current->assign_sibling(sub_diff.get());
    }

    auto cb = sup.call_select_folder(folder->get_id());
    sup.send_model<model::payload::model_update_t>(diff, cb.get());
}

void folder_table_t::on_create() {}

void folder_table_t::on_reset() {
    auto &rows = get_rows();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        auto &row = rows[i];
        auto widget = std::get_if<widgetable_ptr_t>(&row.value);
        if (!widget) {
            continue;
        }
        auto share = dynamic_cast<device_share_widget_t *>(widget->get());
        if (!share) {
            continue;
        }
        bool remove = false;
        auto &device = share->initial_device;
        if (!device) {
            remove = true;
        } else {
            auto device_id = device->device_id().get_sha256();
            remove = !initially_shared_with.by_sha256(device_id);
        }
        if (remove) {
            remove_row(**widget);
            --i;
        }
    }
    reset();
    refresh();
}

void folder_table_t::on_remove() {
    auto r = fl_choice("Are you sure? (no files on disk are touched)", "Yes", "No", nullptr);
    if (r != 0) {
        return;
    }
    auto &sup = container.supervisor;
    auto &cluster = *sup.get_cluster();
    auto &folder = *cluster.get_folders().by_id(folder_data.get_id());
    auto diff = model::diff::cluster_diff_ptr_t{};
    diff = new model::diff::modify::remove_folder_t(cluster, folder);
    sup.send_model<model::payload::model_update_t>(std::move(diff), this);
}

void folder_table_t::on_rescan() {}
