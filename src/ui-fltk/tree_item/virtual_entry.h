#pragma once

#include "model/file_info.h"

namespace syncspirit::fltk::tree_item {

struct virtual_entry_t {
    virtual model::file_info_t *get_entry() = 0;
};

} // namespace syncspirit::fltk::tree_item
