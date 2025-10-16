// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023-2025 Ivan Baidakou

#pragma once

#include <boost/outcome.hpp>

#include "utils/bytes.h"
#include "file.h"
#include "scan_task.h"
#include "syncspirit-export.h"

namespace syncspirit::fs {

namespace outcome = boost::outcome_v2;
namespace bfs = std::filesystem;

struct SYNCSPIRIT_API new_chunk_iterator_t {
    struct block_hash_t {
        utils::bytes_t digest;
        int32_t size;
    };

    using hashes_t = std::vector<block_hash_t>;

    new_chunk_iterator_t(scan_task_ptr_t task, proto::FileInfo metadata, file_ptr_t backend) noexcept;

    bool has_more_chunks() const noexcept;
    outcome::result<details::chunk_t> read() noexcept;
    inline const bfs::path &get_path() noexcept { return backend->get_path(); }

    inline scan_task_ptr_t get_task() noexcept { return task; }
    void ack(std::uint32_t block_index, utils::bytes_t hash, int32_t block_size) noexcept;
    bool is_complete() const noexcept;
    inline hashes_t &get_hashes() noexcept { return hashes; }
    inline int64_t get_size() const noexcept { return file_size; }
    inline int64_t get_block_size() const noexcept { return block_size; }
    inline proto::FileInfo &get_metadata() noexcept { return metadata; }

  private:
    scan_task_ptr_t task;
    proto::FileInfo metadata;
    file_ptr_t backend;
    int64_t last_queued_block;
    int64_t valid_blocks;
    size_t unread_blocks;
    size_t block_size;
    size_t queue_size;
    size_t unread_bytes;
    size_t next_idx;
    int64_t file_size;
    size_t offset;
    std::set<std::int64_t> unfinished;
    hashes_t hashes;
    bool invalid;
};

} // namespace syncspirit::fs
