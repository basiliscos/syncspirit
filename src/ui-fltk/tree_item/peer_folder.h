#pragma once

#include "entry.h"

namespace syncspirit::fltk::tree_item {

struct peer_folder_t final : entry_t {
    using parent_t = entry_t;
    peer_folder_t(model::folder_info_t &folder_info, app_supervisor_t &supervisor, Fl_Tree *tree);

    void update_label() override;
    void on_update() override;
    void on_open() override;
    bool on_select() override;

    model::file_info_t *get_entry() override;
    entry_t *make_entry(model::file_info_t *file, std::string filename) override;

    model::folder_info_t &folder_info;
    bool expandend;
};

} // namespace syncspirit::fltk::tree_item
