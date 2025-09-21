// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023-2025 Ivan Baidakou

#pragma once

#include <boost/outcome.hpp>
#include <rotor/address.hpp>
#include <vector>

#include "model/file_info.h"
#include "utils/bytes.h"
#include "file.h"
#include "scan_task.h"
#include "syncspirit-export.h"

namespace syncspirit::fs {

namespace outcome = boost::outcome_v2;
namespace bfs = std::filesystem;

struct SYNCSPIRIT_API chunk_iterator_t {
    using valid_blocks_map_t = std::vector<bool>;

    chunk_iterator_t(scan_task_ptr_t task,
                     model::file_info_ptr_t file,
                     const model::folder_info_t& peer_folder,
                     file_ptr_t backend) noexcept;

    bool has_more_chunks() const noexcept;
    inline bool is_complete() const noexcept { return unhashed_blocks == 0; }
    inline bool has_valid_blocks() const noexcept { return valid_blocks_count > 0; }
    inline const valid_blocks_map_t &valid_blocks() const noexcept { return valid_blocks_map; }

    void ack_hashing() noexcept;
    void ack_block(utils::bytes_view_t digest, size_t block_index) noexcept;
    outcome::result<details::chunk_t> read() noexcept;

    inline model::file_info_ptr_t get_file() { return peer_file; }
    inline const model::folder_info_t& get_folder() { return peer_folder; }

    inline bfs::path get_path() noexcept { return backend->get_path(); }
    inline outcome::result<void> remove() noexcept { return backend->remove(); }

    inline scan_task_ptr_t get_task() noexcept { return task; }

  private:
    scan_task_ptr_t task;
    const model::folder_info_t& peer_folder;
    model::file_info_ptr_t peer_file;
    file_ptr_t backend;
    int64_t last_queued_block;
    size_t unhashed_blocks;
    valid_blocks_map_t valid_blocks_map;
    std::uint_fast32_t valid_blocks_count;
    bool abandoned;
};

} // namespace syncspirit::fs
