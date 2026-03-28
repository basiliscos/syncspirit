#include "watcher.h"

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher.h"

#if SYNCSPIRIT_WATCHER_KQUEUE

#include "fs/fs_supervisor.h"
#include <fcntl.h>

using namespace syncspirit::fs::platform::bsd;

static void node_cb(int fd, void *data, std::uint32_t flags) {
    auto watcher = reinterpret_cast<watcher_t *>(data);
    watcher->kqueue_callback(fd, flags);
}

auto watcher_t::watch_dir(std::string_view path) noexcept -> outcome::result<int> {
    int fd = open(path.data(), O_RDONLY);
    if (fd == -1) {
        return sys::error_code{errno, sys::system_category()};
    }

    auto sup = static_cast<fs::fs_supervisor_t *>(supervisor);
    auto ctx = static_cast<platform_context_t *>(sup->context);
    auto ok = ctx->backend.watch(fd, node_cb, this, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_DELETE);
    if (!ok) {
        close(fd);
        return sys::error_code{sys::errc::io_error, sys::generic_category()};
    }
    return fd;
}

auto watcher_t::unwatch_dir(int wd) noexcept -> sys::error_code {
    auto sup = static_cast<fs::fs_supervisor_t *>(supervisor);
    auto ctx = static_cast<platform_context_t *>(sup->context);
    ctx->backend.unwatch(wd, EVFILT_VNODE, EV_DELETE, NOTE_WRITE | NOTE_DELETE);
    if (close(wd) == -1) {
        return sys::error_code{errno, sys::system_category()};
    }
}

void watcher_t::kqueue_callback(int wd, std::uint32_t flags) noexcept {
    LOG_TRACE(log, "kqueue_callback, wd = {}, flags = {:#x}", wd, flags);
}

#endif