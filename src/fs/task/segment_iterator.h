// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "fs/file.h"
#include "task.h"

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API segment_iterator_t {
    segment_iterator_t(const r::address_ptr_t &back_addr, hasher::payload::extendended_context_prt_t context,
                       bfs::path path, std::int64_t offset, std::int32_t block_index, std::int32_t block_count,
                       std::int32_t block_size, std::int32_t last_block_size) noexcept;
    bool process(fs_slave_t &fs_slave, execution_context_t &context) noexcept;

    r::address_ptr_t back_addr;
    std::uint32_t blocks_limit;
    bfs::path path;
    std::int64_t offset;
    std::int32_t block_index;
    std::int32_t block_count;
    std::int32_t block_size;
    std::int32_t last_block_size;
    sys::error_code ec;
    file_t file;
    std::int32_t current_block = 0;
    hasher::payload::extendended_context_prt_t context;
};

} // namespace syncspirit::fs::task
