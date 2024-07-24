// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "unknown_folder.h"
#include "../table_widget/checkbox.h"
#include "../static_table.h"

#include <FL/Fl_Check_Button.H>
#include <spdlog/fmt/fmt.h>

using namespace syncspirit;
using namespace model::diff;
using namespace fltk;
using namespace fltk::tree_item;

namespace {

struct my_table_t;

auto static make_checkbox(my_table_t &container, bool value) -> widgetable_ptr_t;

struct my_table_t : static_table_t {
    using parent_t = static_table_t;

    my_table_t(unknown_folder_t &container_, int x, int y, int w, int h) : parent_t(x, y, w, h), container{container_} {
        auto &folder = container.folder;
        auto data = table_rows_t();
        data.push_back({"id", std::string(folder.get_id())});
        data.push_back({"label", std::string(folder.get_label())});
        data.push_back({"index", std::to_string(folder.get_index())});
        data.push_back({"max sequence", std::to_string(folder.get_max_sequence())});
        data.push_back({"read only", make_checkbox(*this, folder.is_read_only())});
        data.push_back({"ignore permissions", make_checkbox(*this, folder.are_permissions_ignored())});
        data.push_back({"ignore delete", make_checkbox(*this, folder.is_deletion_ignored())});
        data.push_back({"disable temp indixes", make_checkbox(*this, folder.are_temp_indixes_disabled())});
        data.push_back({"paused", make_checkbox(*this, folder.is_paused())});
        assign_rows(std::move(data));
    }

    unknown_folder_t &container;
};

struct checkbox_widget_t final : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;

    checkbox_widget_t(my_table_t &container, bool value_) : parent_t(container), value{value_} {}

    Fl_Widget *create_widget(int x, int y, int w, int h) override {
        auto r = parent_t::create_widget(x, y, w, h);
        input->deactivate();
        input->value(value ? 1 : 0);
        return r;
    }

    bool value;
};

inline auto static make_checkbox(my_table_t &container, bool value) -> widgetable_ptr_t {
    return new checkbox_widget_t(container, value);
}

} // namespace

unknown_folder_t::unknown_folder_t(model::unknown_folder_t &folder_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), folder{folder_} {

    auto l = fmt::format("{} ({})", folder.get_label(), folder.get_id());
    label(l.c_str());
}

bool unknown_folder_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new my_table_t(*this, x, y, w, h);
    });
    return true;
}
