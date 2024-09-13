#pragma once

#include "peer_dir_base.h"

namespace syncspirit::fltk::tree_item {

struct peer_folder_t final : peer_dir_base_t {
    using parent_t = peer_dir_base_t;
    peer_folder_t(model::folder_info_t &folder_info, app_supervisor_t &supervisor, Fl_Tree *tree);

    void update_label() override;
    void on_open() override;

    model::file_info_t *get_entry() override;

    model::folder_info_t &folder_info;
    bool expandend;
};

} // namespace syncspirit::fltk::tree_item
