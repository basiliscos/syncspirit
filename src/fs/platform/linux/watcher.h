// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_INOTIFY

#include "fs/platform/unix/watcher.h"
#include "context.h"

namespace syncspirit::fs::platform::linux {

struct SYNCSPIRIT_API watcher_t final : unix::watcher_t {
    using parent_t = unix::watcher_t;
    using parent_t::parent_t;

    void do_initialize(r::system_context_t *ctx) noexcept override;
    void shutdown_finish() noexcept override;

    sys::error_code unwatch_dir(int wd) noexcept override;
    outcome::result<int> watch_dir(std::string_view path) noexcept override;

    void inotify_callback() noexcept;

    platform_context_t::io_guard_t inotify_guard;
};

} // namespace syncspirit::fs::platform::linux

#endif
