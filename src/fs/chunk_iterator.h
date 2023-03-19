// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023 Ivan Baidakou

#pragma once

#include <string_view>
#include <boost/outcome.hpp>

#include "model/file_info.h"
#include "file.h"
#include "scan_task.h"
#include "syncspirit-export.h"

namespace syncspirit::fs {

namespace outcome = boost::outcome_v2;
namespace bfs = boost::filesystem;

struct SYNCSPIRIT_API chunk_iterator_t {

    struct chunk_t {
        std::string data;
        size_t block_index;
    };

    chunk_iterator_t(scan_task_ptr_t task, model::file_info_ptr_t file, model::file_info_ptr_t source_file,
                     file_ptr_t backend) noexcept;

    bool has_more_chunks() const noexcept;
    inline bool is_complete() const noexcept { return unhashed_blocks == 0; }
    inline bool is_valid() const noexcept { return !invalid; }
    inline size_t get_queue_size() const noexcept { return queue_size; }

    void ack_hashing() noexcept;
    bool ack_block(std::string_view digest, size_t block_index) noexcept;
    outcome::result<chunk_t> read() noexcept;
    inline int64_t has_valid_blocks() const noexcept { return valid_blocks; }

    inline model::file_info_ptr_t get_source() { return source_file; }
    inline model::file_info_ptr_t get_file() { return file; }

    inline bfs::path get_path() noexcept { return backend->get_path(); }
    inline outcome::result<void> remove() noexcept { return backend->remove(); }

    inline scan_task_ptr_t get_task() noexcept { return task; }

  private:
    scan_task_ptr_t task;
    model::file_info_ptr_t file;
    model::file_info_ptr_t source_file;
    file_ptr_t backend;
    int64_t last_queued_block;
    int64_t valid_blocks;
    size_t queue_size;
    size_t unhashed_blocks;
    std::set<std::int64_t> out_of_order;
    bool abandoned;
    bool invalid;
};

} // namespace syncspirit::fs
