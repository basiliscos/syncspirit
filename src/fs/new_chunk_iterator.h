// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023 Ivan Baidakou

#pragma once

#include <string_view>
#include <boost/outcome.hpp>

#include "file.h"
#include "scan_task.h"
#include "syncspirit-export.h"

namespace syncspirit::fs {

namespace outcome = boost::outcome_v2;
namespace bfs = boost::filesystem;

struct SYNCSPIRIT_API new_chunk_iterator_t {
    new_chunk_iterator_t(scan_task_ptr_t task, file_ptr_t backend) noexcept;

    bool has_more_chunks() const noexcept;
    outcome::result<details::chunk_t> read() noexcept;
    inline bfs::path get_path() noexcept { return backend->get_path(); }

    inline scan_task_ptr_t get_task() noexcept { return task; }

  private:
    scan_task_ptr_t task;
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
