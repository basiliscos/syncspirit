#pragma once

#include "model/file_info.h"
#include <boost/filesystem.hpp>

namespace syncspirit::fltk::tree_item {

namespace bfs = boost::filesystem;

struct virtual_dir_t {
    virtual ~virtual_dir_t() = default;

    virtual virtual_dir_t *locate_dir(const bfs::path &parent) = 0;
    virtual void add_file(model::file_info_t &file) = 0;
};

} // namespace syncspirit::fltk::tree_item
