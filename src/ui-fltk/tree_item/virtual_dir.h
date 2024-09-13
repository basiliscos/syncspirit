#pragma once

#include "virtual_entry.h"
#include "model/file_info.h"
#include <boost/filesystem.hpp>

namespace syncspirit::fltk::tree_item {

namespace bfs = boost::filesystem;

struct virtual_dir_t : virtual_entry_t {

    using virtual_entry_t::virtual_entry_t;
    virtual ~virtual_dir_t() = default;

    virtual virtual_dir_t *locate_dir(const bfs::path &parent) = 0;
    virtual virtual_dir_t *locate_own_dir(std::string_view name) = 0;
    virtual void add_entry(model::file_info_t &file) = 0;
    virtual void show_deleted(bool value) = 0;
};

} // namespace syncspirit::fltk::tree_item
