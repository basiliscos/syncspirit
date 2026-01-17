// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher.h"

#if SYNCSPIRIT_WATCHER_INOTIFY
#include <sys/inotify.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <algorithm>
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
    subdir_map.clear();
    path_map.clear();
    if (inotify_fd > 0) {
        LOG_TRACE(log, "closing inotify fd = {}", inotify_fd);
        close(inotify_fd);
    }
    parent_t::shutdown_finish();
}

void watcher_t::inotify_callback() noexcept {
    using U = payload::update_type_t;
    char buffer[1024 * (sizeof(struct inotify_event) + NAME_MAX + 1)];
    char name_buff[PATH_MAX];
    auto name_ptr = name_buff + sizeof(name_buff) - 1;
    *name_ptr-- = 0;
    int length = ::read(inotify_fd, buffer, sizeof(buffer));
    LOG_TRACE(log, "inotify callback, read ({}), result = {}", inotify_fd, length);
    if (length < 0) {
        LOG_ERROR(log, "cannot read: {}", strerror(errno));
    }

    auto append_name = [&](std::string_view piece) {
        auto sz = piece.size();
        name_ptr -= sz;
        std::copy(piece.begin(), piece.end(), name_ptr);
    };

    // Process the events
    if (length) {
        auto deadline = clock_t::local_time() + retension;
        for (int i = 0; i < length;) {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            if (event->len) {
                auto type = payload::update_type_internal_t{0};
                if (event->mask & IN_CREATE) {
                    type = payload::update_type::CREATED;
                } else if (event->mask & IN_DELETE) {
                    type = payload::update_type::DELETED;
                }
                if (event->mask & IN_MODIFY) {
                    type = payload::update_type::CONTENT;
                }
                if (event->mask & IN_ATTRIB) {
                    type = payload::update_type::META;
                }
                if (type) {
                    append_name(std::string_view(event->name));
                    auto guard = &path_map[event->wd];
                    auto folder_id = guard->folder_id;
                    while (guard->parent_fd) {
                        *(--name_ptr) = '/';
                        append_name(guard->rel_path);
                        guard = &path_map[guard->parent_fd];
                    }
                    auto rel_sz = (name_buff + sizeof(name_buff) - 2) - name_ptr;
                    auto rel_path = std::string_view(name_ptr, rel_sz);
                    push(deadline, folder_id, rel_path, U::created);
                }
            }
            i += sizeof(struct inotify_event) + event->len;
        }
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
        auto io_guard = ctx->register_callback(inotify_fd, std::move(cb), this);
        auto it = folder_map.emplace(std::string(p.folder_id), p.path);
        auto folder_id = std::string_view(it.first->first);
        auto path_guard = path_guard_t{
            path_str,
            folder_id,
            std::move(io_guard),
            0,
        };
        LOG_TRACE(log, "registering inotify fd = {}", inotify_fd);
        path_map[fd] = std::move(path_guard);
        auto &fds = subdir_map[fd];
        fds.emplace_back(fd);
        p.ec = {};
    }
}

#endif
