// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <string>
#include <utility>
#include <boost/outcome.hpp>
#include <boost/filesystem.hpp>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include "model/file_info.h"
#include "syncspirit-export.h"

namespace syncspirit {
namespace fs {

namespace bfs = boost::filesystem;
namespace sys = boost::system;
namespace outcome = boost::outcome_v2;

struct block_division_t {
    size_t count;
    int32_t size;
};

SYNCSPIRIT_API bfs::path make_temporal(const bfs::path &path) noexcept;
SYNCSPIRIT_API bool is_temporal(const bfs::path &path) noexcept;
SYNCSPIRIT_API block_division_t get_block_size(size_t file_size) noexcept;

struct relative_result_t {
    bfs::path path;
    bool temp;
};

SYNCSPIRIT_API relative_result_t relativize(const bfs::path &path, const bfs::path &root) noexcept;

SYNCSPIRIT_API extern const std::size_t block_sizes_sz;
SYNCSPIRIT_API extern const std::size_t *block_sizes;

} // namespace fs
} // namespace syncspirit
