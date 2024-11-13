#pragma once

#include "entry.h"

namespace syncspirit::fltk::tree_item {

struct folder_t final : entry_t {
    using parent_t = entry_t;

    folder_t(model::folder_t &folder, app_supervisor_t &supervisor, Fl_Tree *tree);

    bool on_select() override;
    void update_label() override;

    model::file_info_t *get_entry() override;
    entry_t *make_entry(model::file_info_t *file, std::string filename) override;

    model::folder_t &folder;
    model::folder_info_t *folder_info;
};

} // namespace syncspirit::fltk::tree_item
