// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"
#include "fs/platform/watcher_base.h"
#include <set>
#include <optional>
#include <unordered_map>
#include <boost/system.hpp>
#include <boost/outcome.hpp>

#if defined(SYNCSPIRIT_WATCHER_INOTIFY) || defined(SYNCSPIRIT_WATCHER_KQUEUE)

namespace syncspirit::fs::platform::unix {

namespace sys = boost::system;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API watcher_t : watcher_base_t {
    using parent_t = watcher_base_t;
    using parent_t::parent_t;

    using file_type_t = bfs::file_type;
    struct path_guard_t {
        std::string path;
        std::string_view folder_id;
        file_type_t file_type;
        int parent_fd;
    };

    struct watch_result_t {
        path_guard_t *guard{nullptr};
        int wd{-1};
    };
    using watch_opt_t = std::optional<watch_result_t>;

    using path_map_t = std::unordered_map<int, path_guard_t>;
    using path_to_wd_t = std::unordered_map<std::string_view, int>;
    using children_t = std::set<int>;
    using subdir_map_t = std::unordered_map<int, children_t>;

    void shutdown_finish() noexcept override;

    void on_watch(message::watch_folder_t &) noexcept override;
    void on_unwatch(message::unwatch_folder_t &) noexcept override;
    void notify(const fs::task::scan_dir_t &) noexcept override;

    void rename_self_descending(int parent_wd, std::string_view prev_path, std::string_view new_path) noexcept;
    watch_opt_t watch_path(std::string_view path, std::string_view folder_id, file_type_t type, int parent) noexcept;
    sys::error_code unwatch_folder(std::string_view folder_id) noexcept;
    sys::error_code unwatch_recurse(std::string_view path) noexcept;
    sys::error_code unwatch_wd(int wd) noexcept;
    void forget(int wd) noexcept;

    virtual std::optional<int> watch_path(std::string_view path, file_type_t type) noexcept = 0;
    virtual sys::error_code unwatch_path(int wd, file_type_t type) noexcept = 0;

    path_map_t path_map;
    subdir_map_t subdir_map;
    path_to_wd_t path_to_wd;
    bool ready = false;
};

} // namespace syncspirit::fs::platform::unix

#endif
