// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_INOTIFY

#include "fs/platform/context_base.h"

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <unordered_map>

namespace syncspirit::fs::platform::unix {

using io_callback_t = void (*)(int, void *);
struct io_context_t {
    io_context_t(io_callback_t callback_, void *data_) : cb(std::move(callback_)), data{data_} {}
    io_context_t(io_context_t &&) = default;
    io_context_t(const io_context_t &) = delete;
    io_callback_t cb;
    void *data;
};
using io_callbacks_map_t = std::unordered_map<int, io_context_t>;

template <typename Backend>
concept Initializable = requires(Backend b) {
    { b.initialize() } -> std::same_as<bool>;
};

template <typename Backend>
concept Destroyable = requires(Backend b) {
    { b.destroy() } -> std::same_as<void>;
};

template <typename Backend>
concept CanUnwatch = requires(Backend b, int fd) {
    { b.unwatch(fd) } -> std::same_as<void>;
};

template <typename Backend>
concept CanWatch = requires(Backend b, int fd) {
    { b.watch(fd) } -> std::same_as<bool>;
};

template <typename Backend>
concept CanPoll = requires(Backend b, std::uint32_t timeout, io_callbacks_map_t &callbacks) {
    { b.poll(timeout, callbacks) } -> std::same_as<bool>;
};

template <typename Backend>
concept Backenedable =
    Initializable<Backend> && Destroyable<Backend> && CanUnwatch<Backend> && CanWatch<Backend> && CanPoll<Backend>;

template <Backenedable Backend> struct SYNCSPIRIT_API platform_context_t : context_base_t {
    using parent_t = context_base_t;
    using backend_t = Backend;

    struct io_guard_t {
        io_guard_t() : ctx{nullptr}, fd{-1} {}
        io_guard_t(platform_context_t *ctx_, int fd_) : ctx{ctx_}, fd{fd_} {};
        io_guard_t(io_guard_t &&other) : ctx{nullptr}, fd{-1} {
            std::swap(ctx, other.ctx);
            std::swap(fd, other.fd);
        }
        ~io_guard_t() {
            if (fd >= 0 && ctx) {
                ctx->backend.unwatch(fd);
                auto it = ctx->io_callbacks.find(fd);
                if (it != ctx->io_callbacks.end()) {
                    ctx->io_callbacks.erase(it);
                }
            }
        }
        io_guard_t &operator=(io_guard_t &&other) noexcept {
            std::swap(ctx, other.ctx);
            std::swap(fd, other.fd);
            return *this;
        }
        platform_context_t *ctx;
        int fd;
    };

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

    platform_context_t(const pt::time_duration &poll_timeout_) noexcept : parent_t(poll_timeout_), async_flag{false} {
        async_pipes[0] = async_pipes[1];

        if (backend.initialize()) {
            int fds[2];
            auto r = pipe(fds);
            if (r == 0) {
                for (int i = 0; i < 2; ++i) {
                    auto fd = fds[i];
                    int flags = fcntl(fd, F_GETFL, 0);
                    if (flags == -1) {
                        LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
                    } else {
                        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                            LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
                        } else {
                            async_pipes[i] = fd;
                        }
                    }
                }

                async_guard = register_callback(async_pipes[0], async_cb, this);
            } else {
                LOG_CRITICAL(log, "cannot pipe(): {}", strerror(errno));
            }
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

    io_guard_t register_callback(int fd, io_callback_t callback, void *data) {
        if (backend.watch(fd)) {
            io_callbacks.emplace(fd, io_context_t(callback, data));
            return io_guard_t(this, fd);
        } else {
            return {};
        }
    }

    bool wait_next_event() noexcept { return backend.poll(determine_wait_ms(), io_callbacks); }

    int async_pipes[2];
    std::atomic_bool async_flag;
    io_callbacks_map_t io_callbacks;
    io_guard_t async_guard;
    backend_t backend;
};

} // namespace syncspirit::fs::platform::unix

#endif
