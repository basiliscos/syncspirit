#pragma once

#include "file_entry.h"

namespace syncspirit::fltk::tree_item {

struct peer_entry_t : file_entry_t {
    using parent_t = file_entry_t;
    using parent_t::parent_t;

    entry_t *create_child(augmentation_entry_t &entry) override;
};

}; // namespace syncspirit::fltk::tree_item
