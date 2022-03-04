#pragma once

#include <string>
#include <utility>
#include <boost/outcome.hpp>
#include <boost/filesystem.hpp>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include "model/file_info.h"

namespace syncspirit {
namespace fs {

namespace bfs = boost::filesystem;
namespace sys = boost::system;
namespace outcome = boost::outcome_v2;

bfs::path make_temporal(const bfs::path &path) noexcept;
bool is_temporal(const bfs::path &path) noexcept;
std::pair<size_t, size_t> get_block_size(size_t file_size) noexcept;

struct relative_result_t {
    bfs::path path;
    bool temp;
};

relative_result_t relativize(const bfs::path &path, const bfs::path &root) noexcept;

extern std::size_t block_sizes_sz;
extern std::size_t *block_sizes;

} // namespace fs
} // namespace syncspirit
