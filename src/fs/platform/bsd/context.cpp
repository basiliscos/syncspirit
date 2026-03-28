// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "context.h"
#include <cassert>
#include <string.h>

#if SYNCSPIRIT_WATCHER_KQUEUE

using namespace syncspirit::fs::platform::bsd;

static void async_cb(int fd, void *data, std::uint32_t) {
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

bsd_backend_t::bsd_backend_t() { log = utils::get_logger("fs.bsd"); }

auto bsd_backend_t::initialize(int pipe_read_fd, void *platform_context) -> io_guard_holder_t {
    struct pipe_guard_t final : io_guard_t {
        using parent_t = io_guard_t;
        using parent_t::parent_t;
        ~pipe_guard_t() {
            if (fd && ctx) {
                auto &backend = reinterpret_cast<platform_context_t *>(ctx)->backend;
                backend.unwatch(fd, EVFILT_READ, EV_DELETE, 0);
            }
        }
    };

    monitor = kqueue();
    if (monitor <= 0) {
        LOG_CRITICAL(log, "cannot create monitor(): {}", strerror(errno));
        return {};
    }
    auto ok = watch(pipe_read_fd, async_cb, platform_context, EVFILT_READ, EV_ADD, 0);
    if (ok) {
        return std::make_unique<pipe_guard_t>(platform_context, pipe_read_fd);
    } else {
        return {};
    }
}

void bsd_backend_t::destroy() {
    if (monitor >= 0) {
        close(monitor);
    }
}

void bsd_backend_t::unwatch(int fd, short filter, u_short flags, u_int fflags) {
    auto &change = events.back();
    auto sz = events.size();

    // EV_SET(&change, fd, EVFILT_VNODE, EV_DELETE, NOTE_WRITE | NOTE_DELETE, 0, nullptr);
    EV_SET(&change, fd, filter, flags, fflags, 0, nullptr);
    assert(flags & EV_DELETE);

    if (int r = kevent(monitor, &change, 1, nullptr, 0, NULL); r == -1) {
        LOG_ERROR(log, "cannot kevent(/del) for fd {}: {}", fd, strerror(errno));
    } else {
        events.resize(sz - 1);
        LOG_DEBUG(log, "kevent(/del), fd = {}", fd);
    }
}

bool bsd_backend_t::watch(int fd, io_callback_t callback, void *data, short filter, u_short flags, u_int fflags) {
    struct kevent change{};
    auto sz = events.size();

    // EV_SET(&change, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_DELETE, 0, nullptr);
    EV_SET(&change, fd, filter, flags, fflags, 0, nullptr);
    assert(flags & EV_ADD);

    if (int r = kevent(monitor, &change, 1, nullptr, 0, NULL); r == -1) {
        LOG_ERROR(log, "cannot kevent(/add) for fd {}: {}", fd, strerror(errno));
        events.resize(sz - 1);
        return false;
    } else {
        LOG_DEBUG(log, "kevent(/add), fd = {}", fd);
        events.push_back(std::move(change));
        io_callbacks.emplace(fd, io_context_t(callback, data));
        return true;
    }
}

bool bsd_backend_t::poll(std::uint32_t timeout) {
    int i = 0;
    assert(io_callbacks.size() == events.size());

    auto seconds = timeout / 1000;
    auto subsecond = timeout % 1000;
    struct timespec tm;
    using nanoseconds_t = decltype(tm.tv_nsec);

    tm.tv_sec = seconds;
    tm.tv_nsec = nanoseconds_t(1000 * 1000) * subsecond;

    auto r = ::kevent(monitor, nullptr, 0, events.data(), static_cast<int>(events.size()), &tm);
    if (r == -1) {
        LOG_WARN(log, "kevent() failed: {}", strerror(errno));
    } else if (r) {
        auto left = r;
        for (auto it = events.begin(); left && it != events.end(); ++it) {
            auto &event = *it;
            if (event.fflags) {
                --left;
                auto fd = static_cast<int>(event.ident);
                auto &ctx = io_callbacks.at(fd);
                ctx.cb(fd, ctx.data, static_cast<std::uint32_t>(event.fflags));
            }
        }
    }
    return r > 0;
}

#endif
