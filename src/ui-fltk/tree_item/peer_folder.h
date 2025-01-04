#pragma once

#include "peer_entry.h"

namespace syncspirit::fltk::tree_item {

struct peer_folder_t final : peer_entry_t {
    using parent_t = peer_entry_t;
    peer_folder_t(model::folder_info_t &folder_info, app_supervisor_t &supervisor, Fl_Tree *tree);

    void update_label() override;
    bool on_select() override;
};

} // namespace syncspirit::fltk::tree_item
