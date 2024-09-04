#pragma once

#include "../tree_item.h"
#include "virtual_dir.h"

namespace syncspirit::fltk::tree_item {

struct peer_dir_base_t : tree_item_t, virtual_dir_t {

    peer_dir_base_t(app_supervisor_t &supervisor, Fl_Tree *tree, bool has_augmentation);

    virtual_dir_t *locate_dir(const bfs::path &parent) override;
    void add_file(model::file_info_t &file) override;

    int direct_dirs_count;
};

} // namespace syncspirit::fltk::tree_item
