// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "model/folder_info.h"
#include "stack.h"
#include "stack_context.h"
#include "fs/task/tasks.h"

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/outcome.hpp>

namespace syncspirit::net::local_keeper {

struct folder_slave_t;
namespace outcome = boost::outcome_v2;

struct folder_context_t : boost::intrusive_ref_counter<folder_context_t, boost::thread_safe_counter> {
    folder_context_t(model::folder_info_ptr_t local_folder, local_keeper::stack_t stack,
                     const bfs::path &initial_path) noexcept;

    void process_stack(stack_context_t &ctx) noexcept;

    int process(complete_scan_t &, stack_context_t &ctx) noexcept;
    int process(unscanned_dir_t &dir, stack_context_t &ctx) noexcept;
    int process(unexamined_t &child_info, stack_context_t &ctx) noexcept;
    int process(suspend_scan_t &item, stack_context_t &ctx) noexcept;
    int process(unsuspend_scan_t &, stack_context_t &ctx) noexcept;
    int process(child_ready_t &info, stack_context_t &ctx) noexcept;
    int process(undo_child_ready_t &info, stack_context_t &ctx) noexcept;
    int process(hash_existing_file_ptr_t &item, stack_context_t &ctx) noexcept;
    int process(hash_new_file_ptr_t &item, stack_context_t &ctx) noexcept;
    int process(hash_incomplete_file_ptr_t &item, stack_context_t &ctx) noexcept;
    int process(removed_dir_t &item, stack_context_t &ctx) noexcept;
    int process(confirmed_deleted_t &item, stack_context_t &ctx);
    int process(incomplete_t &item, stack_context_t &ctx) noexcept;
    int process(rehashed_incomplete_t &item, stack_context_t &ctx) noexcept;
    int process(abort_hashing_t &item, stack_context_t &ctx) noexcept;

    bool post_process(stack_context_t &ctx) noexcept;
    bool post_process(hash_base_t &hash_file, hasher::message::digest_t &msg, stack_context_t &ctx) noexcept;
    void post_process(fs::task::scan_dir_t &task, stack_context_t &ctx) noexcept;
    void post_process(fs::task::segment_iterator_t &task, stack_context_t &ctx);
    void post_process(fs::task::remove_file_t &task, stack_context_t &ctx) noexcept;
    void post_process(fs::task::rename_file_t &task, stack_context_t &ctx) noexcept;
    void post_process(fs::task::noop_t &, stack_context_t &) noexcept;

    void push(fs::task_t task) noexcept;
    bool has_no_tasks() const noexcept;
    int schedule_hash(hash_base_t *item, stack_context_t &ctx) noexcept;
    fs::task_t pop_task() noexcept;
    void handle_scan_error(fs::task::scan_dir_t &task, stack_context_t &ctx) noexcept;

    model::folder_info_ptr_t local_folder;
    local_keeper::stack_t stack;
    utils::logger_t log;
    fs::tasks_t pending_io;
    bool ignore_permissions;
};

using folder_context_ptr_t = boost::intrusive_ptr<folder_context_t>;
using unexamined_items_t = std::vector<unexamined_t>;

outcome::result<folder_context_ptr_t> make_context(model::folder_info_ptr_t local_folder,
                                                   std::string_view start_subdir) noexcept;

folder_context_ptr_t make_context(model::folder_info_ptr_t local_folder, unexamined_items_t items) noexcept;

} // namespace syncspirit::net::local_keeper
