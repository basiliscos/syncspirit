#pragma once

#include "model/file_info.h"

namespace syncspirit::fltk::tree_item {

struct virtual_entry_t {
    virtual model::file_info_t *get_entry() = 0;
    virtual virtual_entry_t *locate_dir(const bfs::path &parent) = 0;
    virtual virtual_entry_t *locate_own_dir(std::string_view name) = 0;
    virtual void add_entry(model::file_info_t &file) = 0;
    virtual void show_deleted(bool value) = 0;
};

} // namespace syncspirit::fltk::tree_item
