// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "proto/proto-fwd.hpp"
#include "scan_task.h"
#include "chunk_iterator.h"
#include "new_chunk_iterator.h"
#include "utils/bytes.h"

#include <rotor.hpp>
#include <memory>
#include <variant>

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

template <typename ReplyType = void> struct payload_base_t {
    outcome::result<ReplyType> result;
    extendended_context_prt_t context;
};

struct block_request_t : payload_base_t<utils::bytes_t> {
    using parent_t = payload_base_t<utils::bytes_t>;
    bfs::path path;
    std::uint64_t offset;
    std::uint64_t block_size;

    inline block_request_t(extendended_context_prt_t context_, bfs::path path_, std::uint64_t offset_,
                           std::uint64_t block_size_) noexcept
        : parent_t{utils::bytes_t{}, std::move(context_)}, path{std::move(path_)}, offset{offset_},
          block_size{block_size_} {}

    block_request_t(const block_request_t &) = delete;
    block_request_t(block_request_t &&) = default;
};

struct remote_copy_t : payload_base_t<void> {
    using parent_t = payload_base_t<void>;
    bfs::path path;
    proto::FileInfoType type;
    std::uint64_t size;
    std::uint32_t permissions;
    std::int64_t modification_s;
    std::string symlink_target;
    bool deleted;
    bool no_permissions;

    inline remote_copy_t(extendended_context_prt_t context_, bfs::path path_, proto::FileInfoType type_,
                         std::uint64_t size_, std::uint32_t permissions_, std::int64_t modification_s_,
                         std::string symlink_target_, bool deleted_, bool no_permissions_) noexcept
        : parent_t{outcome::success(), std::move(context_)}, path{std::move(path_)}, type{type_}, size{size_},
          permissions{permissions_}, modification_s{modification_s_}, symlink_target(std::move(symlink_target_)),
          deleted{deleted_}, no_permissions{no_permissions_} {}

    remote_copy_t(const remote_copy_t &) = delete;
    remote_copy_t(remote_copy_t &&) = default;
};

struct finish_file_t : payload_base_t<void> {
    using parent_t = payload_base_t<void>;
    bfs::path path;
    bfs::path local_path;
    std::uint64_t file_size;
    std::int64_t modification_s;

    inline finish_file_t(extendended_context_prt_t context_, bfs::path path_, bfs::path local_path_,
                         std::uint64_t file_size_, std::int64_t modification_s_) noexcept
        : parent_t{outcome::success(), std::move(context_)}, path{std::move(path_)}, local_path{std::move(local_path_)},
          file_size{file_size_}, modification_s{modification_s_} {}

    finish_file_t(const finish_file_t &) = delete;
    finish_file_t(finish_file_t &&) = default;
};

struct append_block_t : payload_base_t<void> {
    using parent_t = payload_base_t<void>;
    bfs::path path;
    utils::bytes_t data;
    std::uint64_t offset;
    std::uint64_t file_size;

    inline append_block_t(extendended_context_prt_t context_, bfs::path path_, utils::bytes_t data_,
                          std::uint64_t offset_, std::uint64_t file_size_)
        : parent_t{outcome::success(), std::move(context_)}, path{std::move(path_)}, data{std::move(data_)},
          offset{offset_}, file_size{file_size_} {}

    append_block_t(const append_block_t &) = delete;
    append_block_t(append_block_t &&) = default;
};

struct clone_block_t : payload_base_t<void> {
    using parent_t = payload_base_t<void>;

    bfs::path target;
    std::uint64_t target_offset;
    std::uint64_t target_size;
    bfs::path source;
    std::uint64_t source_offset;
    std::uint64_t block_size;

    inline clone_block_t(extendended_context_prt_t context_, bfs::path target_, std::uint64_t target_offset_,
                         std::uint64_t target_size_, bfs::path source_, std::uint64_t source_offset_,
                         std::uint64_t block_size_) noexcept
        : parent_t{outcome::success(), std::move(context_)}, target{std::move(target_)}, target_offset{target_offset_},
          target_size{target_size_}, source{std::move(source_)}, source_offset{source_offset_},
          block_size{block_size_} {}

    clone_block_t(const clone_block_t &) = delete;
    clone_block_t(clone_block_t &&) = default;
};

using io_command_t = std::variant<block_request_t, remote_copy_t, finish_file_t, append_block_t, clone_block_t>;

} // namespace payload

namespace message {

using scan_progress_t = r::message_t<payload::scan_progress_t>;
using rehash_needed_t = r::message_t<payload::rehash_needed_t>;
using hash_anew_t = r::message_t<payload::hash_anew_t>;

using io_command_t = r::message_t<payload::io_command_t>;

} // namespace message

} // namespace syncspirit::fs
