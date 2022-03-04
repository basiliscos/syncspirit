#pragma once

#include <rotor.hpp>
#include "scan_task.h"
#include "file.h"

namespace syncspirit::fs {

namespace r = rotor;

namespace payload {

struct scan_folder_t {
    std::string folder_id;
};

struct scan_progress_t {
    scan_task_ptr_t task;
    std::uint32_t generation;
};

struct rehash_needed_t {
    scan_task_ptr_t task;
    std::uint32_t generation;
    model::file_info_ptr_t file;
    model::file_info_ptr_t source_file;
    file_ptr_t backend;
    int64_t last_queued_block;
    int64_t valid_blocks;
    size_t queue_size;
    std::set<std::int64_t> out_of_order;
    bool abandoned;
    bool invalid;
};

}; // namespace payload

namespace message {

using scan_folder_t = r::message_t<payload::scan_folder_t>;
using scan_progress_t = r::message_t<payload::scan_progress_t>;
using rehash_needed_t = r::message_t<payload::rehash_needed_t>;

} // namespace message

} // namespace syncspirit::fs
