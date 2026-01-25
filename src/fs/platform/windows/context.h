// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_WIN32

#include "fs/platform/context_base.h"

#include <windows.h>
#include <vector>
#include <unordered_map>

namespace syncspirit::fs::platform::windows {

struct SYNCSPIRIT_API platform_context_t : context_base_t {
    using parent_t = context_base_t;
    using parent_t::parent_t;
    using handle_t = HANDLE;
    using handles_t = std::vector<handle_t>;
    using io_callback_t = void (*)(handle_t, void *);
    using close_handle_t = bool (*)(handle_t);
    struct io_context_t {
        io_context_t(platform_context_t::io_callback_t callback_, void *data_);
        io_context_t(io_context_t &&) = default;
        io_context_t(const io_context_t &) = delete;
        io_callback_t cb;
        void *data;
    };
    using io_callbacks_map_t = std::unordered_map<handle_t, io_context_t>;
    struct io_guard_t {
        io_guard_t();
        io_guard_t(platform_context_t *, close_handle_t close_cb_, handle_t handle);
        io_guard_t(io_guard_t &&);
        io_guard_t(const io_guard_t &) = delete;
        ~io_guard_t();

        io_guard_t &operator=(io_guard_t &&) noexcept;
        operator bool() const;

        platform_context_t *ctx;
        close_handle_t close_cb;
        handle_t handle;
    };

    io_guard_t register_callback(handle_t, io_callback_t, close_handle_t, void *data);

    platform_context_t() noexcept;
    ~platform_context_t();

    void notify() noexcept;
    void wait_next_event() noexcept;

    io_callbacks_map_t io_callbacks;
    handles_t handles;
    io_guard_t async_guard;
};

} // namespace syncspirit::fs::platform::windows

#endif
