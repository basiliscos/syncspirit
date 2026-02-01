// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_WIN32

#include "fs/platform/watcher_base.h"
#include "fs/fs_context.h"
#include "model/misc/arc.hpp"

namespace syncspirit::fs::platform::windows {

struct SYNCSPIRIT_API watcher_t : watcher_base_t {
    using parent_t = watcher_base_t;
    using parent_t::parent_t;

    struct path_guard_t : model::arc_base_t<path_guard_t> {
        static constexpr size_t BUFF_SZ = 16 * 1024;
        using io_guard_t = fs_context_t::io_guard_t;

        path_guard_t(std::string folder_id, io_guard_t dir_guard, io_guard_t event_guard) noexcept;
        sys::error_code initiate() noexcept;

        char buff[BUFF_SZ];
        std::string folder_id;
        OVERLAPPED overlapped;
        io_guard_t dir_guard;
        io_guard_t event_guard;
    };
    using path_guard_ptr_t = model::intrusive_ptr_t<path_guard_t>;
    using handle_t = fs_context_t::handle_t;
    using handle_map_t = std::unordered_map<std::string_view, handle_t>;
    using path_map_t = std::unordered_map<handle_t, path_guard_ptr_t>;

    sys::error_code unwatch_dir(std::string_view folder_id) noexcept;

    void shutdown_finish() noexcept override;
    void on_watch(message::watch_folder_t &) noexcept override;
    void on_unwatch(message::unwatch_folder_t &) noexcept override;
    void on_notify(handle_t handle) noexcept;

    handle_map_t handle_map;
    path_map_t path_map;
};

} // namespace syncspirit::fs::platform::windows

#endif