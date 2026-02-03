// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "stack_context.h"
#include "stack.h"
#include "fs/fs_slave.h"
#include <rotor/actor_base.h>

namespace syncspirit::net {

struct local_keeper_t;

}

namespace syncspirit::net::local_keeper {

namespace r = rotor;

struct folder_slave_t;
using allocator_t = std::pmr::polymorphic_allocator<char>;
using F = presentation::presence_t::features_t;

using folder_slave_ptr_t = r::intrusive_ptr_t<folder_slave_t>;
using hash_base_ptr_t = model::intrusive_ptr_t<hash_base_t>;

struct folder_context_t : boost::intrusive_ref_counter<folder_context_t, boost::thread_safe_counter> {
    inline folder_context_t(model::folder_info_ptr_t local_folder_, std::string_view start_subdir_) noexcept
        : local_folder{local_folder_}, start_subdir{start_subdir_} {}
    model::folder_info_ptr_t local_folder;
    std::string start_subdir;
};

using folder_context_ptr_t = boost::intrusive_ptr<folder_context_t>;

struct hash_context_t final : hasher::payload::extendended_context_t {
    hash_context_t(folder_slave_ptr_t slave_, hash_base_ptr_t hash_file_)
        : slave{std::move(slave_)}, hash_file{std::move(hash_file_)} {}

    folder_slave_ptr_t slave;
    hash_base_ptr_t hash_file;
};
using hash_context_ptr_t = r::intrusive_ptr_t<hash_context_t>;

struct rename_context_t final : hasher::payload::extendended_context_t {
    rename_context_t(rehashed_incomplete_t item_) : item(std::move(item_)) {}
    rehashed_incomplete_t item;
};

struct folder_slave_t final : fs::fs_slave_t {
    using local_keeper_ptr_t = r::intrusive_ptr_t<net::local_keeper_t>;

    folder_slave_t(folder_context_ptr_t context_, local_keeper_ptr_t actor_) noexcept;
    ~folder_slave_t();

    sys::error_code initialize() noexcept;
    void process_stack(stack_context_t &ctx) noexcept;
    int schedule_hash(hash_base_t *item, stack_context_t &ctx) noexcept;
    void prepare_task();
    presentation::presence_t *get_presence(presentation::presence_t *parent, const bfs::path &path, bool is_dir);

    int process(complete_scan_t &, stack_context_t &ctx) noexcept;
    int process(unscanned_dir_t &dir, stack_context_t &ctx) noexcept;
    int process(unexamined_t &child_info, stack_context_t &ctx) noexcept;
    int process(suspend_scan_t &item, stack_context_t &ctx) noexcept;
    int process(fatal_error_t &item, stack_context_t &ctx) noexcept;
    int process(unsuspend_scan_t &, stack_context_t &ctx) noexcept;
    int process(child_ready_t &info, stack_context_t &ctx) noexcept;
    int process(hash_existing_file_ptr_t &item, stack_context_t &ctx) noexcept;
    int process(hash_new_file_ptr_t &item, stack_context_t &ctx) noexcept;
    int process(hash_incomplete_file_ptr_t &item, stack_context_t &ctx) noexcept;
    int process(removed_dir_t &item, stack_context_t &ctx) noexcept;
    int process(confirmed_deleted_t &item, stack_context_t &ctx);
    int process(incomplete_t &item, stack_context_t &ctx) noexcept;
    int process(rehashed_incomplete_t &item, stack_context_t &ctx) noexcept;

    bool post_process() noexcept;
    bool post_process(hash_base_t &hash_file, hasher::message::digest_t &msg) noexcept;
    void post_process(fs::task::scan_dir_t &task, stack_context_t &ctx) noexcept;
    void post_process(fs::task::segment_iterator_t &task, stack_context_t &ctx);
    void post_process(fs::task::remove_file_t &task, stack_context_t &ctx) noexcept;
    void post_process(fs::task::rename_file_t &task, stack_context_t &ctx) noexcept;
    void post_process(fs::task::noop_t &, stack_context_t &) noexcept;

    void handle_scan_error(fs::task::scan_dir_t &task, stack_context_t &ctx) noexcept;

    tasks_t pending_io;
    local_keeper::stack_t stack;
    folder_context_ptr_t context;
    local_keeper_ptr_t actor;
    utils::logger_t log;
    bool force_completion = false;
    bool ignore_permissions;
};

} // namespace syncspirit::net::local_keeper
