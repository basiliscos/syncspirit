#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct peer_folders_t : tree_item_t {
    using parent_t = tree_item_t;
    peer_folders_t(model::device_t &peer, app_supervisor_t &supervisor, Fl_Tree *tree);

    void update_label() override;

    augmentation_ptr_t add_folder(model::folder_info_t &folder);
    void remove_child(tree_item_t *child) override;
    model::device_t &peer;
};

} // namespace syncspirit::fltk::tree_item
