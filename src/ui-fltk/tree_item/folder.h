#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct folder_t : tree_item_t {
    using parent_t = tree_item_t;

    folder_t(model::folder_info_t &folder_info, app_supervisor_t &supervisor, Fl_Tree *tree);

    bool on_select() override;
    void update_label() override;
    void refresh_content() override;

    void on_remove();
    void on_apply();
    void on_reset();
    void on_rescan();

    model::folder_info_t &folder_info;
    Fl_Widget *apply_button;
    Fl_Widget *reset_button;
};

} // namespace syncspirit::fltk::tree_item
