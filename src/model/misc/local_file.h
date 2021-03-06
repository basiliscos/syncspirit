// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once
#include <vector>
#include <memory>
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <boost/functional/hash.hpp>
#include "../block_info.h"

namespace std {
template <> struct hash<boost::filesystem::path> {
    size_t operator()(const boost::filesystem::path &p) const { return boost::filesystem::hash_value(p); }
};
} // namespace std

namespace syncspirit::model {

namespace bfs = boost::filesystem;
namespace sys = boost::system;

struct local_file_t {
    enum file_type_t { regular, dir, symlink };
    using blocks_t = std::vector<model::block_info_ptr_t>;
    blocks_t blocks;
    bfs::path symlink_target;
    file_type_t file_type;
    size_t file_size;
    bool temp;
};

struct local_file_map_t {
    using container_t = std::unordered_map<bfs::path, local_file_t>;

    local_file_map_t(const bfs::path &root_) noexcept : root{root_} {}
    bfs::path root;
    sys::error_code ec;
    container_t map;
};

using local_file_map_ptr_t = std::unique_ptr<local_file_map_t>;

} // namespace syncspirit::model
