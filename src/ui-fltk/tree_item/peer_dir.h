#pragma once

#include "peer_dir_base.h"

namespace syncspirit::fltk::tree_item {

struct peer_dir_t final : peer_dir_base_t {
    using parent_t = peer_dir_base_t;
    peer_dir_t(app_supervisor_t &supervisor, Fl_Tree *tree, model::file_info_t &entry);

    model::file_info_t *get_entry() override;

    model::file_info_t &entry;
};

}; // namespace syncspirit::fltk::tree_item
