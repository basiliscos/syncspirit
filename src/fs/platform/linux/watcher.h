// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_INOTIFY

#include "fs/platform/watcher_base.h"
#include "fs/fs_context.h"

namespace syncspirit::fs::platform::linux {

struct SYNCSPIRIT_API watcher_t : watcher_base_t {
    using parent_t = watcher_base_t;
    using parent_t::parent_t;

    void do_initialize(r::system_context_t *ctx) noexcept override;
    void on_watch(message::watch_folder_t &) noexcept override;
    void inotify_callback() noexcept;
    void shutdown_finish() noexcept override;

    struct path_guard_t {
        std::string rel_path;
        std::string_view folder_id;
        fs_context_t::io_guard_t io_guard;
        int parent_fd;
    };
    using path_map_t = std::unordered_map<int, path_guard_t>;
    using subdir_map_t = std::unordered_map<int, std::vector<int>>;

    int inotify_fd = -1;
    path_map_t path_map;
    subdir_map_t subdir_map;
};

} // namespace syncspirit::fs::platform::linux

#endif
