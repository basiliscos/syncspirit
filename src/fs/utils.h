#pragma once

#include <string>
#include <utility>
#include "continuation.h"

namespace syncspirit {
namespace fs {

namespace outcome = boost::outcome_v2;

bfs::path make_temporal(const bfs::path &path) noexcept;
bool is_temporal(const bfs::path &path) noexcept;
std::pair<size_t, size_t> get_block_size(size_t file_size) noexcept;

struct relative_result_t {
    bfs::path path;
    bool temp;
};

relative_result_t relative(const bfs::path &path, const bfs::path &root) noexcept;

} // namespace fs
} // namespace syncspirit
