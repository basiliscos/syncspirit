// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "model/cluster.h"
#include "model/diff/local/io_failure.h"
#include "config/fs.h"
#include "utils/log.h"
#include "file.h"
#include "proto/proto-helpers.h"
#include <rotor.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <filesystem>
#include <list>
#include <variant>
#include <cstdint>

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
    using path_queue_t = std::list<bfs::path>;
    using files_queue_t = std::list<file_info_t>;
    using unknown_files_queue_t = std::list<unknown_file_t>;

    struct send_guard_t {
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

    scan_task_t(model::cluster_ptr_t cluster, std::string_view folder_id, const config::fs_config_t &config) noexcept;
    ~scan_task_t();

    void push(model::diff::cluster_diff_t *model_update, std::int64_t bytes_consumed = 0) noexcept;
    send_guard_t guard(r::actor_base_t &actor, r::address_ptr_t coordinator) noexcept;
    scan_result_t advance() noexcept;
    const std::string &get_folder_id() const noexcept;

  private:
    scan_result_t advance_dir(bfs::path &dir) noexcept;
    scan_result_t advance_file(file_info_t &file) noexcept;
    scan_result_t advance_regular_file(file_info_t &file) noexcept;
    scan_result_t advance_symlink_file(file_info_t &file) noexcept;
    scan_result_t advance_unknown_file(unknown_file_t &file) noexcept;

    std::string folder_id;
    model::folder_ptr_t folder;
    model::cluster_ptr_t cluster;
    model::file_infos_map_t files;
    utils::logger_t log;
    config::fs_config_t config;
    std::int64_t bytes_left;
    std::int64_t files_left;

    path_queue_t dirs_queue;
    files_queue_t files_queue;
    unknown_files_queue_t unknown_files_queue;
    bfs::path root;

    model::diff::cluster_diff_ptr_t update_diff;
    model::diff::cluster_diff_t *current_diff;

    friend struct send_guard_t;
};

using scan_task_ptr_t = boost::intrusive_ptr<scan_task_t>;

} // namespace syncspirit::fs
