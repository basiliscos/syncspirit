// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher.h"

#if SYNCSPIRIT_WATCHER_INOTIFY
#include <sys/inotify.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <boost/nowide/convert.hpp>
#include "fs/fs_supervisor.h"

using namespace syncspirit::fs::platform::linux;
using boost::nowide::narrow;

void watcher_t::do_initialize(r::system_context_t *ctx) noexcept {
    inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        LOG_ERROR(log, "inotify_init failed: {}", strerror(errno));
        return do_shutdown();
    } else {
        int flags = fcntl(inotify_fd, F_GETFL, 0);
        if (flags == -1) {
            LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
        } else {
            if (fcntl(inotify_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
            } else {
                LOG_TRACE(log, "created inotify fd = {}", inotify_fd);
            }
        }
    }
    parent_t::do_initialize(ctx);
}

void watcher_t::shutdown_finish() noexcept {
    root_guard = {};
    if (inotify_fd > 0) {
        LOG_TRACE(log, "closing inotify fd = {}", inotify_fd);
        close(inotify_fd);
    }
    parent_t::shutdown_finish();
}

void watcher_t::inotify_callback() noexcept {
    char buffer[1024 * (sizeof(struct inotify_event) + NAME_MAX + 1)];
    int length = ::read(inotify_fd, buffer, sizeof(buffer));
    LOG_TRACE(log, "inotify callback, read ({}), result = {}", inotify_fd, length);
    if (length < 0) {
        LOG_ERROR(log, "cannot read: {}", strerror(errno));
    }

    // Process the events
    for (int i = 0; i < length;) {
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        if (event->len) {
            if (event->mask & IN_CREATE) {
                LOG_DEBUG(log, "File created: {}", event->name);
            }
            if (event->mask & IN_DELETE) {
                LOG_DEBUG(log, "File deleted: {}", event->name);
            }
            if (event->mask & IN_MODIFY) {
                LOG_DEBUG(log, "File modified: {}", event->name);
            }
            if (event->mask & IN_ATTRIB) {
                LOG_DEBUG(log, "File meta changed: {}", event->name);
            }
        }
        i += sizeof(struct inotify_event) + event->len;
    }
}

void watcher_t::on_watch(message::watch_folder_t &message) noexcept {
    auto &p = message.payload;
    auto &path = p.path;
    LOG_TRACE(log, "on watch on '{}'", narrow(path.wstring()));
    auto sup = static_cast<fs::fs_supervisor_t *>(supervisor);
    auto ctx = static_cast<fs::fs_context_t *>(sup->context);
    auto &path_str = path.native();
    auto fd = ::inotify_add_watch(inotify_fd, path_str.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE | IN_ATTRIB);
    if (fd <= 0) {
        LOG_ERROR(log, "cannot do inotify_add_watch(): {}", strerror(errno));
    } else {
        auto cb = [](auto, void *data) { reinterpret_cast<watcher_t *>(data)->inotify_callback(); };
        root_guard = ctx->register_callback(inotify_fd, std::move(cb), this);
        LOG_TRACE(log, "registering inotify fd = {}", inotify_fd);
        p.ec = {};
    }
}

#endif
