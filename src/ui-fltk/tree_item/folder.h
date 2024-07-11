#pragma once

#include "../tree_item.h"
#include "../table_widget/base.h"
#include <vector>

namespace syncspirit::fltk::tree_item {

struct folder_t : tree_item_t {
    using parent_t = tree_item_t;
    using widgets_t = std::vector<table_widget::table_widget_ptr_t>;

    folder_t(model::folder_info_t &folder_info, app_supervisor_t &supervisor, Fl_Tree *tree);

    bool on_select() override;
    void update_label() override;

    table_widget::table_widget_ptr_t record(table_widget::table_widget_ptr_t);
    void on_remove();
    void on_apply();
    void on_reset();

    model::folder_info_t &folder_info;
    widgets_t widgets;

    Fl_Widget *apply_button;
    Fl_Widget *reset_button;
};

} // namespace syncspirit::fltk::tree_item
