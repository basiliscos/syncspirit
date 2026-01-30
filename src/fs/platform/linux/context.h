// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_INOTIFY

#include "fs/platform/context_base.h"

#include <atomic>
#include <vector>
#include <unordered_map>
#include <sys/epoll.h>

namespace syncspirit::fs::platform::linux {

struct SYNCSPIRIT_API platform_context_t : context_base_t {
    using parent_t = context_base_t;
    using parent_t::parent_t;

    platform_context_t() noexcept;
    ~platform_context_t();

    void notify() noexcept;
    bool wait_next_event() noexcept;

    using io_callback_t = void (*)(int, void *);
    struct io_context_t {
        io_context_t(platform_context_t::io_callback_t callback_, void *data_, int index_);
        io_context_t(io_context_t &&) = default;
        io_context_t(const io_context_t &) = delete;
        io_callback_t cb;
        void *data;
        int index;
    };
    using io_callbacks_map_t = std::unordered_map<int, io_context_t>;
    using events_t = std::vector<epoll_event>;
    struct io_guard_t {
        io_guard_t();
        io_guard_t(platform_context_t *, int fd);
        io_guard_t(io_guard_t &&);
        ~io_guard_t();

        io_guard_t &operator=(io_guard_t &&) noexcept;
        operator bool() const;
        platform_context_t *ctx;
        int fd;
    };

    io_guard_t register_callback(int, io_callback_t, void *data);

    int epoll_fd;
    int async_pipes[2];
    std::atomic_bool async_flag;
    io_callbacks_map_t io_callbacks;
    events_t events;
    io_guard_t async_guard;
};

} // namespace syncspirit::fs::platform::linux

#endif
