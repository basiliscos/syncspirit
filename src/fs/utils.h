#pragma once

#include <string>
#include "continuation.h"

namespace syncspirit {
namespace fs {

namespace outcome = boost::outcome_v2;

model::block_info_ptr_t compute(payload::scan_t::next_block_t &block) noexcept;
outcome::result<payload::scan_t::next_block_option_t> prepare(const bfs::path &file_path) noexcept;

bfs::path make_temporal(const bfs::path &path) noexcept;
bool is_temporal(const bfs::path &path) noexcept;

struct relative_result_t {
    bfs::path path;
    bool temp;
};

relative_result_t relative(const bfs::path &path, const bfs::path &root) noexcept;

} // namespace fs
} // namespace syncspirit
