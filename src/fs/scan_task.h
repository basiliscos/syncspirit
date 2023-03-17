// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "model/cluster.h"
#include "model/messages.h"
#include "config/fs.h"
#include "utils/log.h"
#include "syncspirit-export.h"
#include <rotor.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/filesystem.hpp>
#include <list>
#include <variant>
#include <optional>

namespace syncspirit::fs {

namespace r = rotor;
namespace bfs = boost::filesystem;
namespace sys = boost::system;

using scan_error_t = model::io_error_t;
using scan_errors_t = model::io_errors_t;

struct unchanged_meta_t {
    model::file_info_ptr_t file;
};

struct changed_meta_t {
    model::file_info_ptr_t file;
};

struct incomplete_t {
    model::file_info_ptr_t file;
};

struct incomplete_removed_t {
    model::file_info_ptr_t file;
};

struct file_error_t {
    model::file_info_ptr_t file;
    sys::error_code ec;
};

struct unknown_file_t {
    bfs::path path;
};

using scan_result_t = std::variant<bool, scan_errors_t, changed_meta_t, unchanged_meta_t, incomplete_t,
                                   incomplete_removed_t, unknown_file_t, file_error_t>;

struct SYNCSPIRIT_API scan_task_t : boost::intrusive_ref_counter<scan_task_t, boost::thread_unsafe_counter> {
    struct file_info_t {
        model::file_info_ptr_t file;
        bool temp;
    };

    using path_queue_t = std::list<bfs::path>;
    using files_queue_t = std::list<file_info_t>;
    using unknown_files_queue_t = std::list<unknown_file_t>;

    scan_task_t(model::cluster_ptr_t cluster, std::string_view folder_id, const config::fs_config_t &config) noexcept;
    ~scan_task_t();

    scan_result_t advance() noexcept;
    std::string_view get_folder_id() const noexcept;

  private:
    scan_result_t advance_dir(const bfs::path &dir) noexcept;
    scan_result_t advance_file(const file_info_t &file) noexcept;

    std::string folder_id;
    model::cluster_ptr_t cluster;
    model::file_infos_map_t *files;
    utils::logger_t log;
    config::fs_config_t config;

    path_queue_t dirs_queue;
    files_queue_t files_queue;
    unknown_files_queue_t unknown_files_queue;
    bfs::path root;
};

using scan_task_ptr_t = boost::intrusive_ptr<scan_task_t>;

} // namespace syncspirit::fs
