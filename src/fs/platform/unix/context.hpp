// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if defined(SYNCSPIRIT_WATCHER_INOTIFY) || defined(SYNCSPIRIT_WATCHER_KQUEUE)

#include "fs/platform/context_base.h"

#include <fcntl.h>
#include <unistd.h>

#include <atomic>

namespace syncspirit::fs::platform::unix {
struct io_guard_t {
    io_guard_t() : ctx{nullptr}, fd{-1} {}
    io_guard_t(void *ctx_, int fd_) : ctx{ctx_}, fd{fd_} {};

    io_guard_t &operator=(io_guard_t &&other) noexcept {
        std::swap(ctx, other.ctx);
        std::swap(fd, other.fd);
        return *this;
    }
    void *ctx;
    int fd;
};

template <typename Backend> struct SYNCSPIRIT_API platform_context_t : context_base_t {
    using parent_t = context_base_t;
    using backend_t = Backend;
    using io_callback_t = typename backend_t::io_callback_t;
    using io_callbacks_map_t = typename backend_t::io_callbacks_map_t;
    using io_guard_t = typename backend_t::io_guard_t;
    using io_guard_holder_t = typename backend_t::io_guard_holder_t;

    platform_context_t(const pt::time_duration &poll_timeout_) noexcept : parent_t(poll_timeout_), async_flag{false} {
        async_pipes[0] = async_pipes[1] = -1;

        int fds[2];
        auto r = pipe(fds);
        if (r == 0) {
            for (int i = 0; i < 2; ++i) {
                auto fd = fds[i];
                int flags = fcntl(fd, F_GETFL, 0);
                if (flags == -1) {
                    LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
                    return;
                } else {
                    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                        LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
                    } else {
                        async_pipes[i] = fd;
                    }
                }
            }

            auto read_fd = async_pipes[0];
            async_guard = backend.initialize(read_fd, this);
        } else {
            LOG_CRITICAL(log, "cannot pipe(): {}", strerror(errno));
        }
    }

    ~platform_context_t() {
        async_guard = {};
        for (int i = 0; i < 2; ++i) {
            if (async_pipes[i] >= 0) {
                close(async_pipes[i]);
            }
        }
        backend.destroy();
    }

    void notify() noexcept {
        if (!async_flag.load(std::memory_order_acquire)) {
            async_flag.store(true, std::memory_order_release);
            write(async_pipes[1], &(async_pipes[1]), 1);
        }
    }

    template <typename... Args>
    io_guard_t register_callback(int fd, io_callback_t callback, void *data, Args &&...args) {
        if (backend.watch(fd, callback, data, std::forward<Args>(args)...)) {
            return io_guard_t(this, fd);
        } else {
            return {};
        }
    }

    bool wait_next_event() noexcept { return backend.poll(determine_wait_ms()); }
    void poll_events() noexcept override { backend.poll(0); }

    int async_pipes[2];
    std::atomic_bool async_flag;
    io_guard_holder_t async_guard;
    backend_t backend;
};

} // namespace syncspirit::fs::platform::unix

#endif
