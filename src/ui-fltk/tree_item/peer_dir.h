#pragma once

#include "peer_dir_base.h"

namespace syncspirit::fltk::tree_item {

struct peer_dir_t final : peer_dir_base_t {
    using parent_t = peer_dir_base_t;
    using parent_t::parent_t;
};

}; // namespace syncspirit::fltk::tree_item
