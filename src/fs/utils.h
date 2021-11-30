#pragma once

#include <string>
#include <utility>
#include <boost/outcome.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/smart_ptr/local_shared_ptr.hpp>


namespace syncspirit {
namespace fs {

namespace bio = boost::iostreams;
namespace bfs = boost::filesystem;
namespace sys = boost::system;
namespace outcome = boost::outcome_v2;

struct mmaped_file_t: boost::intrusive_ref_counter<mmaped_file_t, boost::thread_unsafe_counter> {
    using backend_t = boost::local_shared_ptr<bio::mapped_file>;

    mmaped_file_t() noexcept;
    mmaped_file_t(const bfs::path&, backend_t backend, bool temporal) noexcept;

    const bfs::path& get_path() const noexcept;
    operator bool() const noexcept;
    char* data() noexcept;
    const char* data() const noexcept;

    backend_t get_backend() noexcept;

    outcome::result<void> close() noexcept;

private:
    bfs::path path;
    backend_t backend;
    bool temporal;
};

using mmaped_file_ptr_t = boost::intrusive_ptr<mmaped_file_t>;


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
