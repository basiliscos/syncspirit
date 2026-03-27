// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_KQUEUE

#include "fs/platform/unix/context.hpp"
#include "utils/log.h"
#include <sys/event.h>
#include <cstdint>

namespace syncspirit::fs::platform::bsd {

struct SYNCSPIRIT_API bsd_backend_t {
    using events_t = std::vector<struct kevent>;
    using io_callback_t = void (*)(int, void *, std::uint32_t);
    struct io_context_t {
        io_context_t(io_callback_t callback_, void *data_) : cb(std::move(callback_)), data{data_} {}
        io_context_t(io_context_t &&) = default;
        io_context_t(const io_context_t &) = delete;
        io_callback_t cb;
        void *data;
    };
    using io_callbacks_map_t = std::unordered_map<int, io_context_t>;

    bsd_backend_t();
    bool initialize(int pipe_read_fd, void *platform_context);
    void destroy();

    bool watch(int, io_callback_t, void *, short filter, u_short flags, u_int fflags);
    void unwatch(int);

    bool poll(std::uint32_t timeout);

    int monitor{-1};
    utils::logger_t log;
    events_t events;
    io_callbacks_map_t io_callbacks;
};

namespace details {
using base_t = platform::unix::platform_context_t<bsd_backend_t>;
}

struct SYNCSPIRIT_API platform_context_t : details::base_t {
    using parent_t = details::base_t;
    using parent_t::parent_t;
};

} // namespace syncspirit::fs::platform::bsd

#endif
