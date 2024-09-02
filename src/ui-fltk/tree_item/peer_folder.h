#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct peer_folder_t : tree_item_t {
    using parent_t = tree_item_t;
    peer_folder_t(model::folder_info_t &folder_info, app_supervisor_t &supervisor, Fl_Tree *tree);

    void update_label() override;
    void on_open() override;

    model::folder_info_t &folder_info;
    bool expandend;
};

} // namespace syncspirit::fltk::tree_item
