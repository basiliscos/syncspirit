#pragma once

#include "entry.h"

namespace syncspirit::fltk::tree_item {

struct local_entry_t final : entry_t {
    using parent_t = entry_t;
    local_entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, model::file_info_t &entry);

    bool on_select() override;
    void update_label() override;
    void on_update() override;

    model::file_info_t *get_entry() override;
    entry_t *make_entry(model::file_info_t &file) override;

    model::file_info_t &entry;
};

}; // namespace syncspirit::fltk::tree_item
