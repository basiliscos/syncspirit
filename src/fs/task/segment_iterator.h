// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "fs/file.h"
#include "hasher/messages.h"
#include <cstdint>
#include <filesystem>
#include <boost/system.hpp>
#include <rotor.hpp>

namespace syncspirit::fs {

namespace bfs = std::filesystem;
namespace sys = boost::system;
namespace r = rotor;

struct fs_slave_t;

namespace task {

struct SYNCSPIRIT_API segment_iterator_t {
    segment_iterator_t(const r::address_ptr_t &hasher_addr, const r::address_ptr_t &back_addr,
                       hasher::payload::extendended_context_prt_t context, bfs::path path, std::int64_t offset,
                       std::int32_t block_index, std::int32_t block_count, std::int32_t block_size,
                       std::int32_t last_block_size) noexcept;
    void process(fs_slave_t &fs_slave, r::actor_base_t &host) noexcept;

    r::address_ptr_t hasher_addr;
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

} // namespace task

} // namespace syncspirit::fs
