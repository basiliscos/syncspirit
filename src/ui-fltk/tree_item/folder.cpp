#include "folder.h"

#include "../table_widget/checkbox.h"
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

struct checkbox_widget_t : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;
    using parent_t::parent_t;

    Fl_Widget *create_widget(int x, int y, int w, int h) override {
        auto r = parent_t::create_widget(x, y, w, h);
        input->callback([](auto, void *data) { reinterpret_cast<folder_t *>(data)->refresh_content(); }, &container);
        return r;
    }
};

inline auto static make_read_only(folder_t &container) -> table_widget::table_widget_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
            input->value(container.folder_info.get_folder()->is_read_only());
        }
    };
    return new widget_t(container);
}

inline auto static make_ignore_permissions(folder_t &container) -> table_widget::table_widget_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
            input->value(container.folder_info.get_folder()->are_permissions_ignored());
        }
    };
    return new widget_t(container);
}

inline auto static make_ignore_delete(folder_t &container) -> table_widget::table_widget_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
            input->value(container.folder_info.get_folder()->is_deletion_ignored());
        }
    };
    return new widget_t(container);
}

inline auto static make_disable_tmp(folder_t &container) -> table_widget::table_widget_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
            input->value(container.folder_info.get_folder()->are_temp_indixes_disabled());
        }
    };
    return new widget_t(container);
}

inline auto static make_paused(folder_t &container) -> table_widget::table_widget_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_t &>(this->container);
            input->value(container.folder_info.get_folder()->is_paused());
        }
    };
    return new widget_t(container);
}

inline auto static make_actions(folder_t &container) -> table_widget::table_widget_ptr_t {
    struct widget_t final : table_widget::base_t {
        using parent_t = table_widget::base_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = 100, hh = h - padding * 2;
            auto apply = new Fl_Button(x + padding, yy, ww, hh, "apply");
            auto reset = new Fl_Button(apply->x() + ww + padding * 2, yy, ww, hh, "reset");
            auto remove = new Fl_Button(reset->x() + ww + padding * 2, yy, ww, hh, "remove");
            apply->deactivate();
            reset->deactivate();
            remove->color(FL_RED);
            group->end();
            widget = group;

            apply->callback([](auto, void *data) { static_cast<folder_t *>(data)->on_apply(); }, &container);
            reset->callback([](auto, void *data) { static_cast<folder_t *>(data)->on_reset(); }, &container);
            remove->callback([](auto, void *data) { static_cast<folder_t *>(data)->on_remove(); }, &container);

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
    folder->serialize(ctx.folder);
    folder_info.serialize(ctx.folder_info);

    auto folder_data = ctx.folder.SerializeAsString();
    auto folder_info_data = ctx.folder_info.SerializeAsString();

    bool valid = true;
    for (auto &w : widgets) {
        valid = valid && w->store(&ctx);
    }

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
        data.push_back({"label", std::string(f->get_label())});
        data.push_back({"entries", std::to_string(entries)});
        data.push_back({"index", std::to_string(folder_info.get_index())});
        data.push_back({"max sequence", std::to_string(folder_info.get_max_sequence())});
        data.push_back({"read only", record(make_read_only(*this))});
        data.push_back({"ignore permissions", record(make_ignore_permissions(*this))});
        data.push_back({"ignore delete", record(make_ignore_delete(*this))});
        data.push_back({"disable temp indixes", record(make_disable_tmp(*this))});
        data.push_back({"paused", record(make_paused(*this))});
        data.push_back({"actions", record(make_actions(*this))});

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        content = new static_table_t(std::move(data), x, y, w, h);
        return content;
    });
    refresh_content();
    return true;
}

auto folder_t::record(table_widget::table_widget_ptr_t widget) -> table_widget::table_widget_ptr_t {
    widgets.push_back(widget);
    return widget;
}

void folder_t::on_remove() {}

void folder_t::on_apply() {}

void folder_t::on_reset() {
    for (auto &w : widgets) {
        w->reset();
    }
    refresh_content();
}
