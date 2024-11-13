#pragma once

#include "file_entry.h"

namespace syncspirit::fltk::tree_item {

struct peer_entry_t final : file_entry_t {
    using parent_t = file_entry_t;
    using parent_t::parent_t;
};

}; // namespace syncspirit::fltk::tree_item
