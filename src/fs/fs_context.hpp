// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <rotor/thread.hpp>
#include "syncspirit-export.h"
#include "syncspirit-config.h"
#include "utils/log.h"

#if SYNCSPIRIT_WATCHER_INOTIFY
#include <atomic>
#include <unordered_map>
#include <sys/epoll.h>
#elif SYNCSPIRIT_WATCHER_WIN32
#include <windows.h>
#endif

namespace syncspirit::fs {

namespace r = rotor;
namespace rth = rotor::thread;

struct SYNCSPIRIT_API fs_context_t : rth::system_context_thread_t {
    using parent_t = rth::system_context_thread_t;

    fs_context_t() noexcept;
    ~fs_context_t();

    void run() noexcept override;
    bool wait_next_event() noexcept;

#if SYNCSPIRIT_WATCHER_INOTIFY
    using io_callback_t = void (*)(int, void *);
    struct io_context_t {
        io_context_t(fs_context_t::io_callback_t callback_, void *data_, int index_);
        io_callback_t cb;
        void *data;
        int index;
    };
    using io_callbacks_map_t = std::unordered_map<int, io_context_t>;
    using events_t = std::vector<epoll_event>;
    struct io_guard_t {
        io_guard_t();
        io_guard_t(fs_context_t *, int fd);
        io_guard_t(io_guard_t &&);
        ~io_guard_t();

        io_guard_t &operator=(io_guard_t &&);
        operator bool() const;
        fs_context_t *ctx;
        int fd;
    };

    io_guard_t register_callback(int, io_callback_t, void *data);

    int epoll_fd;
    int async_pipes[2];
    std::atomic_bool async_flag;
    io_callbacks_map_t io_callbacks;
    events_t events;
    io_guard_t async_guard;
#elif SYNCSPIRIT_WATCHER_WIN32
    HANDLE async_event;
#endif
    utils::logger_t log;
};

} // namespace syncspirit::fs
