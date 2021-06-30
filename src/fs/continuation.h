#pragma once
#include "messages.h"

#include <deque>
#include <optional>
#include <boost/outcome.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/iostreams/stream.hpp>
#include <memory>
#include <fstream>

namespace syncspirit {
namespace fs {

namespace bio = boost::iostreams;

using request_ptr_t = r::intrusive_ptr_t<message::scan_request_t>;

namespace payload {

struct scan_t {
    using file_t = bio::mapped_file_source;
    using file_ptr_t = std::unique_ptr<file_t>;

    struct next_block_t {
        bfs::path path;
        std::size_t block_size;
        std::size_t file_size;
        std::size_t block_index;
        file_ptr_t file;
    };

    using next_block_option_t = std::optional<next_block_t>;

    request_ptr_t request;
    model::block_infos_map_t blocks_map;
    std::deque<bfs::path> scan_dirs;
    std::deque<bfs::path> files_queue;
    model::local_file_map_ptr_t file_map;
    std::optional<next_block_t> next_block;
};

struct process_signal_t {};

} // namespace payload

namespace message {

using scan_t = r::message_t<payload::scan_t>;
using process_signal_t = r::message_t<payload::process_signal_t>;

} // namespace message

} // namespace fs
} // namespace syncspirit
