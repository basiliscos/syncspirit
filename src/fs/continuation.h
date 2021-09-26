#pragma once
#include "messages.h"

#include <deque>
#include <optional>
#include <boost/outcome.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <memory>
#include <fstream>
#include <unordered_set>
#include <functional>

namespace syncspirit {
namespace fs {

using request_ptr_t = r::intrusive_ptr_t<message::scan_request_t>;

namespace payload {

struct file_t {
    inline file_t(const bfs::path &path_, const bfs::path &rel_path_, bool temp_) noexcept
        : path{path_}, rel_path{rel_path_}, temp{temp_} {}

    bfs::path path;
    bfs::path rel_path;
    bio::mapped_file mapped_file;
    size_t file_size = 0;
    size_t procesed_sz = 0;
    size_t next_block_sz = 0;
    size_t next_block_idx = 0;
    bool temp = false;
    std::vector<model::block_info_ptr_t> blocks;
};

using file_ptr_t = boost::local_shared_ptr<file_t>;

struct block_task_t : boost::intrusive_ref_counter<block_task_t, boost::thread_unsafe_counter> {
    file_ptr_t file;
    size_t block_idx;
    size_t block_sz;
    void *backref;
    inline block_task_t(const file_ptr_t &file_, size_t block_idx_, size_t block_sz_, void *backref_) noexcept
        : file{file_}, block_idx{block_idx_}, block_sz{block_sz_}, backref{backref_} {}
};
using block_task_ptr_t = boost::intrusive_ptr<block_task_t>;

} // namespace payload
} // namespace fs
} // namespace syncspirit

namespace std {
template <> struct hash<syncspirit::fs::payload::block_task_ptr_t> {
    std::size_t operator()(const syncspirit::fs::payload::block_task_ptr_t &bt) const noexcept {
        return (std::size_t)bt.get();
    }
};
} // namespace std

namespace syncspirit {
namespace fs {
namespace payload {

struct scan_t {
    using block_tasks_t = std::unordered_set<block_task_ptr_t>;
    using block_task_map_t = std::unordered_map<const void *, block_tasks_t>;

    request_ptr_t request;
    model::block_infos_map_t blocks_map;
    std::deque<bfs::path> scan_dirs;
    std::deque<bfs::path> files_queue;
    model::local_file_map_ptr_t file_map;
    file_ptr_t current_file;
    block_task_map_t block_task_map;
};

struct process_signal_t {};

} // namespace payload

namespace message {

using scan_t = r::message_t<payload::scan_t>;
using process_signal_t = r::message_t<payload::process_signal_t>;

} // namespace message

} // namespace fs
} // namespace syncspirit
