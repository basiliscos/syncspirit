#include "folder.h"

#include "../table_widget/checkbox.h"
#include "../table_widget/choice.h"
#include "../table_widget/input.h"
#include <vector>
#include <spdlog/fmt/fmt.h>

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

static constexpr int padding = 2;

namespace {

struct serialiazation_context_t {
    db::Folder folder;
    db::FolderInfo folder_info;
};

struct my_table_t : static_table_t {
    using parent_t = static_table_t;

    my_table_t(folder_t &container_, unsigned shared_count_, table_rows_t &&rows, int x, int y, int w, int h)
        : parent_t(std::move(rows), x, y, w, h), container{container_}, shared_count{shared_count_} {}

    bool on_remove_share(widgetable_t &item) {
        bool removed = false;
        if (shared_count > 1) {
            --shared_count;
            parent_t::remove_row(item);
            container.refresh_content();
            removed = true;
        }
        redraw();
        return removed;
    }

    void on_add_share(widgetable_t &) {}

    unsigned shared_count;
    folder_t &container;
};

struct checkbox_widget_t : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;
    using parent_t::parent_t;

    Fl_Widget *create_widget(int x, int y, int w, int h) override {
        auto r = parent_t::create_widget(x, y, w, h);
        input->callback([](auto, void *data) { reinterpret_cast<folder_t *>(data)->refresh_content(); }, &container);
        return r;
    }
};

inline auto static make_label(folder_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::input_t {
        using parent_t = table_widget::input_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->callback([](auto, void *data) { reinterpret_cast<folder_t *>(data)->refresh_content(); },
                            &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
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

inline auto static make_folder_type(folder_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::choice_t {
        using parent_t = table_widget::choice_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->size(200, r->h());
            input->callback([](auto, void *data) { reinterpret_cast<folder_t *>(data)->refresh_content(); },
                            &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            input->add("Send and Receive");
            input->add("Send only");
            input->add("Receive only");
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
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

inline auto static make_pull_order(folder_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::choice_t {
        using parent_t = table_widget::choice_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->size(200, r->h());
            input->callback([](auto, void *data) { reinterpret_cast<folder_t *>(data)->refresh_content(); },
                            &container);
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
            auto &container = static_cast<folder_t &>(this->container);
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

inline auto static make_read_only(folder_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
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

inline auto static make_ignore_permissions(folder_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
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

inline auto static make_ignore_delete(folder_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
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

inline auto static make_disable_tmp(folder_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
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

inline auto static make_paused(folder_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
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

inline auto static make_shared_with(folder_t &container, model::device_t &device) -> widgetable_ptr_t {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;

        widget_t(tree_item_t &container, model::device_t &device_)
            : parent_t(container), device{device_}, input{nullptr} {}

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
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
                    auto self = reinterpret_cast<widget_t *>(data);
                    auto &container = static_cast<folder_t &>(self->container);
                    auto table = static_cast<my_table_t *>(container.content);
                    table->on_add_share(*self);
                },
                this);
            remove->callback(
                [](auto, void *data) {
                    auto self = reinterpret_cast<widget_t *>(data);
                    if (self->input->value()) {
                        auto &container = static_cast<folder_t &>(self->container);
                        auto table = static_cast<my_table_t *>(container.content);
                        bool ok = table->on_remove_share(*self);
                        if (!ok) {
                            self->input->value(0);
                        }
                    }
                },
                this);

            group->end();
            group->resizable(nullptr);
            widget = group;
            reset();
            return widget;
        }

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
            auto cluster = container.supervisor.get_cluster();

            input->add("(empty)");
            int i = 1;
            int index = i;
            for (auto &it : cluster->get_devices()) {
                auto device = it.item.get();
                if (device == cluster->get_device().get()) {
                    continue;
                }
                auto short_id = device->device_id().get_short();
                auto label = fmt::format("{}, {}", device->get_name(), short_id);
                input->add(label.data());
                if (device == &this->device) {
                    index = i;
                }
                ++i;
            }
            input->value(index);
        }

        bool store(void *data) override {
            // auto ctx = reinterpret_cast<serialiazation_context_t *>(data);
            // ctx->folder.set_paused(input->value());
            return true;
        }

        model::device_t &device;
        Fl_Choice *input;
    };
    return new widget_t(container, device);
}

inline auto static make_actions(folder_t &container) -> widgetable_ptr_t {
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
            auto &container = static_cast<folder_t &>(this->container);
            container.apply_button = apply;
            container.reset_button = reset;
            return widget;
        }
    };

    return new widget_t(container);
}

} // namespace

folder_t::folder_t(model::folder_info_t &folder_info_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree, true), folder_info{folder_info_}, apply_button{nullptr}, reset_button{nullptr} {
    update_label();
}

void folder_t::update_label() {
    auto f = folder_info.get_folder();
    auto value = fmt::format("{}, {}", f->get_label(), f->get_id());
    label(value.data());
    tree()->redraw();
}

void folder_t::refresh_content() {
    if (!content) {
        return;
    }

    serialiazation_context_t ctx;
    auto folder = folder_info.get_folder();
    folder_info.serialize(ctx.folder_info);
    folder->serialize(ctx.folder);

    auto folder_data = ctx.folder.SerializeAsString();
    auto folder_info_data = ctx.folder_info.SerializeAsString();
    auto valid = static_cast<static_table_t *>(content)->store(&ctx);

    auto is_same =
        (folder_data == ctx.folder.SerializeAsString()) && (folder_info_data == ctx.folder_info.SerializeAsString());
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

bool folder_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        auto data = table_rows_t();
        auto f = folder_info.get_folder();
        auto entries = folder_info.get_file_infos().size();

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

        auto cluster = supervisor.get_cluster();
        unsigned shared_count = 0;
        for (auto it : cluster->get_devices()) {
            if (it.item != cluster->get_device()) {
                ++shared_count;
                data.push_back({"shared_with", make_shared_with(*this, *it.item)});
            }
        }
        data.push_back({"actions", make_actions(*this)});

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        content = new my_table_t(*this, shared_count, std::move(data), x, y, w, h);
        return content;
    });
    refresh_content();
    return true;
}

void folder_t::on_remove() {}

void folder_t::on_apply() {}

void folder_t::on_rescan() {}

void folder_t::on_reset() {
    static_cast<static_table_t *>(content)->reset();
    refresh_content();
}
