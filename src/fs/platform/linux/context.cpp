// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "context.h"
#include <cassert>

#if SYNCSPIRIT_WATCHER_INOTIFY

using namespace syncspirit::fs::platform::linux;

linux_backend_t::linux_backend_t() { log = utils::get_logger("fs.linux_backend"); }

bool linux_backend_t::initialize() {
    monitor = epoll_create1(0);
    if (monitor <= 0) {
        LOG_CRITICAL(log, "cannot create monitor(): {}", strerror(errno));
        return false;
    }
    return true;
}

void linux_backend_t::destroy() {
    if (monitor >= 0) {
        close(monitor);
    }
}

void linux_backend_t::unwatch(int fd) {
    auto code = epoll_ctl(monitor, EPOLL_CTL_DEL, fd, nullptr);
    LOG_DEBUG(log, "epoll_ctl(DEL), fd = {}, code = {}", fd, code);
    events.resize(events.size() - 1);
}

bool linux_backend_t::watch(int fd) {
    auto &event = events.emplace_back(epoll_event{});
    event.data.fd = fd;
    event.events = EPOLLIN;
    auto sz = events.size();
    if (epoll_ctl(monitor, EPOLL_CTL_ADD, fd, &event) == -1) {
        LOG_ERROR(log, "cannot epoll_ctl(ADD) for {}: {}", fd, strerror(errno));
        events.resize(sz - 1);
        return false;
    } else {
        LOG_DEBUG(log, "epoll_ctl(ADD), fd = {}", fd);
        return true;
    }
}

bool linux_backend_t::poll(std::uint32_t timeout, unix::io_callbacks_map_t &io_callbacks) {
    int i = 0;
    assert(io_callbacks.size() == events.size());
    for (auto &[fd, i_ctx] : io_callbacks) {
        auto &event = events[i++];
        event.data.fd = fd;
        event.events = EPOLLIN;
    }

    auto r = ::epoll_wait(monitor, events.data(), i, timeout);
    if (r == -1) {
        LOG_WARN(log, "epoll_wait() failed: {}", strerror(errno));
    } else if (r) {
        auto left = r;
        for (auto it = events.begin(); left && it != events.end(); ++it) {
            auto &event = *it;
            if (event.events & EPOLLIN) {
                --left;
                auto fd = event.data.fd;
                auto &ctx = io_callbacks.at(fd);
                ctx.cb(fd, ctx.data);
            }
        }
    }
    return r > 0;
}

#endif
