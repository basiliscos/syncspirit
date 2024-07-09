#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct folders_t : tree_item_t {
    using parent_t = tree_item_t;
    folders_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    augmentation_ptr_t add_folder(model::folder_info_t &folder);

    // bool on_select() override;
    void update_label() override;
};

} // namespace syncspirit::fltk::tree_item
