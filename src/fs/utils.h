// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <string>
#include <utility>
#include <boost/outcome.hpp>
#include <filesystem>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include "model/file_info.h"
#include "syncspirit-export.h"

namespace syncspirit {
namespace fs {

namespace bfs = std::filesystem;
namespace sys = boost::system;
namespace outcome = boost::outcome_v2;

using fs_time_t = std::filesystem::file_time_type;

struct block_division_t {
    size_t count;
    int32_t size;
};

SYNCSPIRIT_API bfs::path make_temporal(const bfs::path &path) noexcept;
SYNCSPIRIT_API bool is_temporal(const bfs::path &path) noexcept;
SYNCSPIRIT_API block_division_t get_block_size(size_t file_size, int32_t prev_size) noexcept;
SYNCSPIRIT_API bfs::path relativize(const bfs::path &path, const bfs::path &root) noexcept;
SYNCSPIRIT_API std::int64_t to_unix(const fs_time_t &at);
SYNCSPIRIT_API fs_time_t from_unix(std::int64_t at);

SYNCSPIRIT_API extern const std::size_t block_sizes_sz;
SYNCSPIRIT_API extern const std::size_t *block_sizes;
SYNCSPIRIT_API extern const std::string_view tmp_suffix;

} // namespace fs
} // namespace syncspirit
