// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "context.h"
#include <cassert>

#if SYNCSPIRIT_WATCHER_INOTIFY

using namespace syncspirit::fs::platform::linux;

static void async_cb(int fd, void *data) {
    auto ctx = reinterpret_cast<platform_context_t *>(data);
    char dummy[4];
    bool do_read = true;
    while (do_read) {
        auto r = ::read(fd, &dummy, sizeof(dummy));
        do_read = r > 0;
        if (r < 0) {
            if (errno != EAGAIN) {
                LOG_WARN(ctx->log, "read() failed: {}", strerror(errno));
            }
        }
    }
    ctx->async_flag.store(false, std::memory_order_release);
}

linux_backend_t::io_guard_t::~io_guard_t() {
    if (fd && ctx) {
        reinterpret_cast<platform_context_t *>(ctx)->backend.unwatch(fd);
    }
}

linux_backend_t::linux_backend_t() { log = utils::get_logger("fs.linux"); }

auto linux_backend_t::initialize(int pipe_read_fd, void *platform_context) -> io_guard_holder_t {
    monitor = epoll_create1(0);
    if (monitor <= 0) {
        LOG_CRITICAL(log, "cannot create monitor(): {}", strerror(errno));
        return {};
    }

    auto ok = watch(pipe_read_fd, async_cb, platform_context);
    if (ok) {
        return io_guard_t(platform_context, pipe_read_fd);
    }
    return {};
}

void linux_backend_t::destroy() {
    if (monitor >= 0) {
        close(monitor);
    }
}

void linux_backend_t::unwatch(int fd) {
    auto code = epoll_ctl(monitor, EPOLL_CTL_DEL, fd, nullptr);
    LOG_DEBUG(log, "epoll_ctl(DEL), fd = {}, code = {}", fd, code);

    auto it = io_callbacks.find(fd);
    if (it != io_callbacks.end()) {
        io_callbacks.erase(it);
    }

    events.resize(events.size() - 1);
}

bool linux_backend_t::watch(int fd, io_callback_t callback, void *data) {
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
        io_callbacks.emplace(fd, io_context_t(callback, data));
        return true;
    }
}

bool linux_backend_t::poll(std::uint32_t timeout) {
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
