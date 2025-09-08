// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "model/cluster.h"
#include "model/diff/local/io_failure.h"
#include "config/fs.h"
#include "utils/log.h"
#include "utils/string_comparator.hpp"
#include "file_cache.h"
#include "proto/proto-fwd.hpp"
#include <rotor.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <filesystem>
#include <variant>
#include <stack>
#include <cstdint>

#include <unordered_map>

namespace syncspirit::fs {

namespace r = rotor;
namespace bfs = std::filesystem;
namespace sys = boost::system;

using scan_error_t = model::diff::local::io_error_t;
using scan_errors_t = model::diff::local::io_errors_t;

struct unchanged_meta_t {
    model::file_info_ptr_t file;
};

struct changed_meta_t {
    model::file_info_ptr_t file;
    proto::FileInfo metadata;
};

struct incomplete_t {
    model::file_info_ptr_t file;
    file_ptr_t opened_file;
};

struct incomplete_removed_t {
    model::file_info_ptr_t file;
};

struct orphaned_removed_t {
    bfs::path path;
};

struct removed_t {
    model::file_info_ptr_t file;
};

struct file_error_t {
    bfs::path path;
    sys::error_code ec;
};

struct unknown_file_t {
    bfs::path path;
    proto::FileInfo metadata;
};

using scan_result_t = std::variant<bool, scan_errors_t, changed_meta_t, unchanged_meta_t, incomplete_t, removed_t,
                                   incomplete_removed_t, orphaned_removed_t, unknown_file_t, file_error_t>;

struct SYNCSPIRIT_API scan_task_t : boost::intrusive_ref_counter<scan_task_t, boost::thread_unsafe_counter> {
    using file_info_t = model::file_info_ptr_t;
    using seen_paths_t = std::unordered_map<std::string, bfs::path, utils::string_hash_t, utils::string_eq_t>;

    struct SYNCSPIRIT_API send_guard_t {
        send_guard_t(scan_task_t &task, r::actor_base_t &actor, r::address_ptr_t coordinator) noexcept;
        ~send_guard_t();

        void send_by_force() noexcept;
        void send_progress() noexcept;

        scan_task_t &task;
        r::actor_base_t &actor;
        r::address_ptr_t coordinator;
        bool force_send;
        bool manage_progress;
    };

    scan_task_t(model::cluster_ptr_t cluster, std::string_view folder_id, file_cache_ptr_t rw_cache,
                const config::fs_config_t &config) noexcept;

    void push(model::diff::cluster_diff_t *model_update, std::int64_t bytes_consumed = 0,
              bool consumes_file = false) noexcept;
    send_guard_t guard(r::actor_base_t &actor, r::address_ptr_t coordinator) noexcept;
    scan_result_t advance() noexcept;
    const std::string &get_folder_id() const noexcept;
    const seen_paths_t &get_seen_paths() const noexcept;

  private:
    using diffs_t = std::vector<model::diff::cluster_diff_ptr_t>;

    struct file_t {
        file_info_t file;
    };

    struct unseen_dir_t {
        using next_t = std::variant<std::monostate, file_t, unknown_file_t>;
        bfs::path path;
        next_t next;
    };

    using queue_item_t = std::variant<file_t, unknown_file_t, unseen_dir_t>;
    struct comparator_t {
        bool operator()(const queue_item_t &lhs, const queue_item_t &rhs) const noexcept;
    };

    using stack_t = std::stack<queue_item_t>;

    scan_result_t do_advance(std::monostate item) noexcept;
    scan_result_t do_advance(file_t item) noexcept;
    scan_result_t do_advance(unseen_dir_t item) noexcept;
    scan_result_t do_advance(unknown_file_t item) noexcept;

    scan_result_t advance_regular_file(file_info_t &file) noexcept;
    scan_result_t advance_symlink_file(file_info_t &file) noexcept;

    std::string folder_id;
    model::folder_ptr_t folder;
    model::cluster_ptr_t cluster;
    file_cache_ptr_t rw_cache;
    model::file_infos_map_t files;
    utils::logger_t log;
    config::fs_config_t config;
    std::int64_t files_limit;
    std::int64_t bytes_left;
    std::int64_t files_left;

    stack_t stack;
    bfs::path root;
    seen_paths_t seen_paths;

    model::diff::cluster_diff_ptr_t update_diff;
    model::diff::cluster_diff_t *current_diff;
    std::uint_fast32_t diff_siblings;
    diffs_t diffs;

    bool ignore_permissions = false;

    friend struct send_guard_t;
};

using scan_task_ptr_t = boost::intrusive_ptr<scan_task_t>;

} // namespace syncspirit::fs
