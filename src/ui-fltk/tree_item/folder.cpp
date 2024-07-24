#include "folder.h"

#include "../table_widget/checkbox.h"
#include "../table_widget/choice.h"
#include "../table_widget/input.h"
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <spdlog/fmt/fmt.h>
#include <vector>

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

static constexpr int padding = 2;

namespace {

struct my_table_t;

auto static make_label(my_table_t &container) -> widgetable_ptr_t;
auto static make_folder_type(my_table_t &container) -> widgetable_ptr_t;
auto static make_pull_order(my_table_t &container) -> widgetable_ptr_t;
auto static make_read_only(my_table_t &container) -> widgetable_ptr_t;
auto static make_ignore_permissions(my_table_t &container) -> widgetable_ptr_t;
auto static make_ignore_delete(my_table_t &container) -> widgetable_ptr_t;
auto static make_disable_tmp(my_table_t &container) -> widgetable_ptr_t;
auto static make_paused(my_table_t &container) -> widgetable_ptr_t;
auto static make_actions(my_table_t &container) -> widgetable_ptr_t;
auto static make_shared_with(my_table_t &container, model::device_ptr_t device) -> widgetable_ptr_t;

using shared_devices_t = boost::local_shared_ptr<model::devices_map_t>;

struct serialiazation_context_t {
    db::Folder folder;
    db::FolderInfo folder_info;
    model::devices_map_t shared_with;
};

struct device_share_widget_t final : widgetable_t {
    using parent_t = widgetable_t;
    device_share_widget_t(Fl_Widget &container, model::device_ptr_t device_);

    Fl_Widget *create_widget(int x, int y, int w, int h) override;
    void reset() override;
    bool store(void *data) override;

    model::device_ptr_t initial_device;
    model::device_ptr_t device;
    shared_devices_t shared_with;
    shared_devices_t non_shared_with;
    Fl_Choice *input;
};

struct my_table_t : static_table_t {
    using parent_t = static_table_t;

    my_table_t(folder_t &container_, int x, int y, int w, int h)
        : parent_t(x, y, w, h), container{container_}, folder_info(container_.folder_info),
          shared_with{new model::devices_map_t()}, non_shared_with{new model::devices_map_t()}, apply_button{nullptr},
          reset_button{nullptr} {

        auto data = table_rows_t();
        auto f = folder_info.get_folder();
        auto entries = folder_info.get_file_infos().size();

        auto shared_devices = shared_devices_t(new model::devices_map_t());
        auto non_shared_devices = shared_devices_t(new model::devices_map_t());

        data.push_back({"path", f->get_path().string()});
        data.push_back({"id", std::string(f->get_id())});
        data.push_back({"label", make_label(*this)});
        data.push_back({"type", make_folder_type(*this)});
        data.push_back({"pull order", make_pull_order(*this)});
        data.push_back({"entries", std::to_string(entries)});
        data.push_back({"index", std::to_string(folder_info.get_index())});
        data.push_back({"max sequence", std::to_string(folder_info.get_max_sequence())});
        data.push_back({"read only", make_read_only(*this)});
        data.push_back({"ignore permissions", make_ignore_permissions(*this)});
        data.push_back({"ignore delete", make_ignore_delete(*this)});
        data.push_back({"disable temp indixes", make_disable_tmp(*this)});
        data.push_back({"paused", make_paused(*this)});

        auto cluster = container.supervisor.get_cluster();
        for (auto it : cluster->get_devices()) {
            auto &device = it.item;
            if (device != cluster->get_device()) {
                if (f->is_shared_with(*device)) {
                    shared_devices->put(device);
                    auto widget = make_shared_with(*this, device);
                    data.push_back({"shared_with", widget});
                } else {
                    non_shared_devices->put(device);
                }
            }
        }
        data.push_back({"actions", make_actions(*this)});

        initially_shared_with = *shared_with;
        initially_non_shared_with = *non_shared_with;
        assign_rows(std::move(data));
    }

    bool on_remove_share(widgetable_t &widget, model::device_ptr_t device, model::device_ptr_t initial) {
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

    void on_select(model::device_ptr_t device, model::device_ptr_t previous) {
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

    void on_add_share(widgetable_t &widget) {
        auto [from_index, count] = scan(widget);
        if (count < container.supervisor.get_cluster()->get_devices().size() - 1) {
            assert(from_index);
            auto w = widgetable_ptr_t{};
            w.reset(new device_share_widget_t(*this, {}));
            insert_row("shared with", w, from_index + 1);
        }
    }

    std::pair<int, int> scan(widgetable_t &widget) {
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

    void refresh() override {
        serialiazation_context_t ctx;
        auto folder = folder_info.get_folder();
        folder_info.serialize(ctx.folder_info);
        folder->serialize(ctx.folder);

        auto folder_data = ctx.folder.SerializeAsString();
        auto folder_info_data = ctx.folder_info.SerializeAsString();
        auto valid = store(&ctx);

        // clang-format off
        auto is_same = (folder_data == ctx.folder.SerializeAsString())
                    && (folder_info_data == ctx.folder_info.SerializeAsString())
                    && (initially_shared_with == ctx.shared_with);
        // clang-format on
        if (!is_same) {
            if (valid) {
                apply_button->activate();
            }
            reset_button->activate();
        } else {
            apply_button->deactivate();
            reset_button->deactivate();
        }
    }

    model::devices_map_t initially_shared_with;
    model::devices_map_t initially_non_shared_with;
    shared_devices_t shared_with;
    shared_devices_t non_shared_with;
    folder_t &container;
    model::folder_info_t &folder_info;
    Fl_Widget *apply_button;
    Fl_Widget *reset_button;
};

device_share_widget_t::device_share_widget_t(Fl_Widget &container, model::device_ptr_t device_)
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
            auto &table = static_cast<my_table_t &>(self->container);
            table.on_add_share(*self);
        },
        this);
    remove->callback(
        [](auto, void *data) {
            auto self = reinterpret_cast<device_share_widget_t *>(data);
            auto &table = static_cast<my_table_t &>(self->container);
            self->device = {};
            bool ok = table.on_remove_share(*self, self->device, self->initial_device);
            if (!ok) {
                self->input->value(0);
            }
        },
        this);
    input->callback(
        [](auto, void *data) {
            auto self = reinterpret_cast<device_share_widget_t *>(data);
            auto &table = static_cast<my_table_t &>(self->container);
            auto previous = self->device;
            if (self->input->value()) {
                auto cluster = table.container.supervisor.get_cluster();
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
            table.on_select(self->device, previous);
        },
        this);

    group->end();
    group->resizable(nullptr);
    widget = group;
    reset();
    return widget;
}

void device_share_widget_t::reset() {
    auto &container = static_cast<my_table_t &>(this->container);
    auto cluster = container.container.supervisor.get_cluster();

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
    if (index == 1) {
        index = 0;
    }
    input->value(index);
}

bool device_share_widget_t::store(void *data) {
    if (device) {
        auto ctx = reinterpret_cast<serialiazation_context_t *>(data);
        ctx->shared_with.put(device);
    }
    return true;
}

struct checkbox_widget_t : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;
    using parent_t::parent_t;

    Fl_Widget *create_widget(int x, int y, int w, int h) override {
        auto r = parent_t::create_widget(x, y, w, h);
        input->callback([](auto, void *data) { reinterpret_cast<my_table_t *>(data)->refresh(); }, &container);
        return r;
    }
};

auto static make_label(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::input_t {
        using parent_t = table_widget::input_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->callback([](auto, void *data) { reinterpret_cast<my_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            return r;
        }

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            auto value = container.folder_info.get_folder()->get_label();
            input->value(value.data());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<serialiazation_context_t *>(data);
            ctx->folder.set_label(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_folder_type(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::choice_t {
        using parent_t = table_widget::choice_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->size(200, r->h());
            input->callback([](auto, void *data) { reinterpret_cast<my_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            input->add("Send and Receive");
            input->add("Send only");
            input->add("Receive only");
            return r;
        }

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            auto value = container.folder_info.get_folder()->get_folder_type();
            input->value(static_cast<int>(value));
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<serialiazation_context_t *>(data);
            auto value = (db::FolderType)(input->value());

            ctx->folder.set_folder_type(value);
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_pull_order(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::choice_t {
        using parent_t = table_widget::choice_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->size(200, r->h());
            input->callback([](auto, void *data) { reinterpret_cast<my_table_t *>(data)->refresh(); }, &container);
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
            auto &container = static_cast<my_table_t &>(this->container);
            auto value = container.folder_info.get_folder()->get_pull_order();
            input->value(static_cast<int>(value));
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<serialiazation_context_t *>(data);
            auto value = (db::PullOrder)(input->value());

            ctx->folder.set_pull_order(value);
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_read_only(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            input->value(container.folder_info.get_folder()->is_read_only());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<serialiazation_context_t *>(data);
            ctx->folder.set_read_only(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_ignore_permissions(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            input->value(container.folder_info.get_folder()->are_permissions_ignored());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<serialiazation_context_t *>(data);
            ctx->folder.set_ignore_permissions(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_ignore_delete(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            input->value(container.folder_info.get_folder()->is_deletion_ignored());
        }
        bool store(void *data) override {
            auto ctx = reinterpret_cast<serialiazation_context_t *>(data);
            ctx->folder.set_ignore_delete(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_disable_tmp(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            input->value(container.folder_info.get_folder()->are_temp_indixes_disabled());
        }
        bool store(void *data) override {
            auto ctx = reinterpret_cast<serialiazation_context_t *>(data);
            ctx->folder.set_disable_temp_indexes(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_paused(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            input->value(container.folder_info.get_folder()->is_paused());
        }
        bool store(void *data) override {
            auto ctx = reinterpret_cast<serialiazation_context_t *>(data);
            ctx->folder.set_paused(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_shared_with(my_table_t &container, model::device_ptr_t device) -> widgetable_ptr_t {
    return new device_share_widget_t(container, device);
}

auto static make_actions(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = 100, hh = h - padding * 2;
            auto apply = new Fl_Button(x + padding, yy, ww, hh, "apply");
            auto reset = new Fl_Button(apply->x() + ww + padding * 2, yy, ww, hh, "reset");
            auto remove = new Fl_Button(reset->x() + ww + padding * 2, yy, ww, hh, "remove");
            auto rescan = new Fl_Button(remove->x() + ww + padding * 2, yy, ww, hh, "rescan");
            apply->deactivate();
            reset->deactivate();
            remove->color(FL_RED);
            group->end();
            widget = group;

            apply->callback([](auto, void *data) { static_cast<folder_t *>(data)->on_apply(); }, &container);
            reset->callback([](auto, void *data) { static_cast<folder_t *>(data)->on_reset(); }, &container);
            remove->callback([](auto, void *data) { static_cast<folder_t *>(data)->on_remove(); }, &container);
            rescan->callback([](auto, void *data) { static_cast<folder_t *>(data)->on_rescan(); }, &container);

            this->reset();
            auto &container = static_cast<my_table_t &>(this->container);
            container.apply_button = apply;
            container.reset_button = reset;
            return widget;
        }
    };

    return new widget_t(container);
}

} // namespace

folder_t::folder_t(model::folder_info_t &folder_info_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree, true), folder_info{folder_info_} {
    update_label();
}

void folder_t::update_label() {
    auto f = folder_info.get_folder();
    auto value = fmt::format("{}, {}", f->get_label(), f->get_id());
    label(value.data());
    tree()->redraw();
}

bool folder_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        return new my_table_t(*this, prev->x(), prev->y(), prev->w(), prev->h());
    });
    return true;
}

void folder_t::on_remove() {}

void folder_t::on_apply() {}

void folder_t::on_rescan() {}

void folder_t::on_reset() {
    static_cast<static_table_t *>(content)->reset();
    refresh_content();
}
