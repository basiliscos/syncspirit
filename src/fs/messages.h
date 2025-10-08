// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <rotor.hpp>
#include <memory>
#include "proto/proto-fwd.hpp"
#include "scan_task.h"
#include "chunk_iterator.h"
#include "new_chunk_iterator.h"
#include "utils/bytes.h"

namespace syncspirit::fs {

namespace r = rotor;

namespace payload {

struct scan_progress_t {
    scan_task_ptr_t task;
};

using rehash_needed_t = chunk_iterator_t;
using hash_anew_t = new_chunk_iterator_t;

struct extendended_context_t {
    virtual ~extendended_context_t() = default;
};

using extendended_context_prt_t = std::unique_ptr<extendended_context_t>;

struct block_request_t {
    bfs::path path;
    std::uint64_t offset;
    std::uint64_t block_size;
    outcome::result<utils::bytes_t> result;
};

struct remote_copy_t {
    bfs::path path;
    proto::FileInfoType type;
    std::uint64_t size;
    std::uint32_t permissions;
    std::int64_t modification_s;
    std::string symlink_target;
    bool deleted;
    bool no_permissions;
    outcome::result<void> result;
    extendended_context_prt_t context;
};

struct finish_file_t {
    bfs::path path;
    bfs::path local_path;
    std::uint64_t file_size;
    std::int64_t modification_s;
    outcome::result<void> result;
    extendended_context_prt_t context;
};

struct append_block_t {
    bfs::path path;
    utils::bytes_view_t data;
    std::uint64_t offset;
    std::uint64_t file_size;
    outcome::result<void> result;
    extendended_context_prt_t context;
};

struct clone_block_t {
    bfs::path target;
    std::uint64_t target_offset;
    std::uint64_t target_size;
    bfs::path source;
    std::uint64_t source_offset;
    std::uint64_t block_size;
    outcome::result<void> result;
    extendended_context_prt_t context;
};

} // namespace payload

namespace message {

using scan_progress_t = r::message_t<payload::scan_progress_t>;
using rehash_needed_t = r::message_t<payload::rehash_needed_t>;
using hash_anew_t = r::message_t<payload::hash_anew_t>;

using block_request_t = r::message_t<payload::block_request_t>;
using remote_copy_t = r::message_t<payload::remote_copy_t>;
using finish_file_t = r::message_t<payload::finish_file_t>;
using append_block_t = r::message_t<payload::append_block_t>;
using clone_block_t = r::message_t<payload::clone_block_t>;

} // namespace message

} // namespace syncspirit::fs
