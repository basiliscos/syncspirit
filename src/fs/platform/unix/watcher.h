// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"
#include "fs/platform/watcher_base.h"
#include <set>
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

    struct path_guard_t {
        std::string path;
        std::string_view folder_id;
        int parent_fd;
    };

    struct watch_result_t {
        sys::error_code ec{};
        path_guard_t *guard{nullptr};
        int wd{-1};
    };

    using path_map_t = std::unordered_map<int, path_guard_t>;
    using path_to_wd_t = std::unordered_map<std::string_view, int>;
    using children_t = std::set<int>;
    using subdir_map_t = std::unordered_map<int, children_t>;

    void shutdown_finish() noexcept override;

    void on_watch(message::watch_folder_t &) noexcept override;
    void on_unwatch(message::unwatch_folder_t &) noexcept override;
    void notify(const fs::task::scan_dir_t &) noexcept override;

    void rename_self_descending(int parent_wd, std::string_view prev_path, std::string_view new_path) noexcept;
    watch_result_t watch_recurse(std::string_view path, std::string_view folder_id, int parent_fd) noexcept;
    sys::error_code unwatch_recurse(std::string_view folder_id) noexcept;
    watch_result_t watch_dir(std::string_view path, std::string_view folder_id, int parent) noexcept;
    void forget(int wd) noexcept;

    virtual sys::error_code unwatch_dir(int wd) noexcept = 0;
    virtual outcome::result<int> watch_dir(std::string_view path) noexcept = 0;

    path_map_t path_map;
    subdir_map_t subdir_map;
    path_to_wd_t path_to_wd;
    bool ready = false;
};

} // namespace syncspirit::fs::platform::unix

#endif
