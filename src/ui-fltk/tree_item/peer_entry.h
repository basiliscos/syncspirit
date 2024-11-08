#pragma once

#include "peer_entry_base.h"

namespace syncspirit::fltk::tree_item {

struct peer_entry_t final : peer_entry_base_t {
    using parent_t = peer_entry_base_t;
    peer_entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, model::file_info_t &entry);

    model::file_info_t *get_entry() override;
    bool on_select() override;
    void update_label() override;
    void on_update() override;
    peer_entry_base_t *make_entry(model::file_info_t &file) override;

    model::file_info_t &entry;
};

}; // namespace syncspirit::fltk::tree_item
