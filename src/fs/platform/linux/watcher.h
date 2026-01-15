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

    int inotify_fd = -1;
    fs_context_t::io_guard_t root_guard;
};

} // namespace syncspirit::fs::platform::linux

#endif
