// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_INOTIFY

#include "fs/platform/watcher_base.h"
#include "fs/fs_context.h"
#include <set>
#include <tuple>

namespace syncspirit::fs::platform::linux {

struct SYNCSPIRIT_API watcher_t : watcher_base_t {
    using parent_t = watcher_base_t;
    using parent_t::parent_t;

    void do_initialize(r::system_context_t *ctx) noexcept override;
    void on_watch(message::watch_folder_t &) noexcept override;
    void on_unwatch(message::unwatch_folder_t &) noexcept override;
    void inotify_callback() noexcept;
    void shutdown_finish() noexcept override;
    void notify(const fs::task::scan_dir_t &) noexcept override;

    struct path_guard_t {
        std::string path;
        std::string_view folder_id;
        int parent_fd;
    };
    using path_map_t = std::unordered_map<int, path_guard_t>;
    using path_to_wd_t = std::unordered_map<std::string_view, int>;
    using children_t = std::set<int>;
    using watch_result_t = std::tuple<sys::error_code, path_guard_t *, int>;
    using subdir_map_t = std::unordered_map<int, children_t>;

    watch_result_t watch_dir(std::string_view path, std::string_view folder_id, int parent) noexcept;
    watch_result_t watch_recurse(std::string_view path, std::string_view folder_id, int parent_fd) noexcept;
    sys::error_code unwatch_recurse(std::string_view folder_id) noexcept;
    void forget(int wd) noexcept;
    void rename_self_descending(int parent_wd, std::string_view prev_path, std::string_view new_path) noexcept;

    fs_context_t::io_guard_t io_guard;
    path_map_t path_map;
    subdir_map_t subdir_map;
    path_to_wd_t path_to_wd;
};

} // namespace syncspirit::fs::platform::linux

#endif
