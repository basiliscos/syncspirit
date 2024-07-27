#pragma once

#include "../tree_item.h"
#include "../static_table.h"

namespace syncspirit::fltk::tree_item {

struct folder_t : tree_item_t {
    using parent_t = tree_item_t;

    folder_t(model::folder_info_t &folder_info, app_supervisor_t &supervisor, Fl_Tree *tree);

    bool on_select() override;
    void update_label() override;

    model::folder_info_t &folder_info;
};

} // namespace syncspirit::fltk::tree_item
