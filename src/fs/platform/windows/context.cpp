// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "utils/platform.h"
#include "context.h"

#if SYNCSPIRIT_WATCHER_WIN32
#include <boost/system.hpp>
#include <utility>

using namespace syncspirit;
using namespace syncspirit::fs::platform::windows;
namespace sys = boost::system;
using guard_t = platform_context_t::io_guard_t;
using io_ctx_t = platform_context_t::io_context_t;

guard_t::io_guard_t() : ctx{nullptr}, handle{nullptr} {}
guard_t::io_guard_t(platform_context_t *ctx_, close_handle_t close_cb_, handle_t handle_)
    : ctx{ctx_}, close_cb{close_cb_}, handle{handle_} {}
guard_t::io_guard_t(io_guard_t &&other) : ctx{nullptr}, close_cb{nullptr}, handle{nullptr} {
    std::swap(ctx, other.ctx);
    std::swap(close_cb, other.close_cb);
    std::swap(handle, other.handle);
}
guard_t::~io_guard_t() {
    if (handle && ctx && close_cb) {
        auto ok = close_cb(handle);
        if (!ok) {
            auto log = utils::get_logger("fs");
            auto ec = sys::error_code(::GetLastError(), sys::system_category());
            LOG_WARN(log, "cannot close handle {}: {}", (void *)handle, ec.message());
        }
    }
}
guard_t &guard_t::operator=(guard_t &&other) noexcept {
    std::swap(ctx, other.ctx);
    std::swap(close_cb, other.close_cb);
    std::swap(handle, other.handle);
    return *this;
}
guard_t::operator bool() const { return (bool)handle; }

io_ctx_t::io_context_t(platform_context_t::io_callback_t callback_, void *data_) : cb{callback_}, data{data_} {}

static void async_cb(HANDLE handle, void *data) {
    auto ctx = reinterpret_cast<platform_context_t *>(data);
    ::ResetEvent(handle);
    if (auto ok = ::ResetEvent(handle); !ok) {
        auto ec = sys::error_code(::GetLastError(), sys::system_category());
        auto log = utils::get_logger("fs");
        LOG_WARN(log, "cannot reset asyn handle {} : {}", (void *)handle, ec.message());
    }
}

static bool close_handle_cb(HANDLE handle) { return ::CloseHandle(handle); }

platform_context_t::platform_context_t(const pt::time_duration &poll_timeout_) noexcept : parent_t(poll_timeout_) {
    auto event = ::CreateEvent(nullptr, false, false, nullptr);
    if (!event) {
        auto ec = sys::error_code(::GetLastError(), sys::system_category());
        LOG_CRITICAL(log, "cannot CreateEvent(): {}", ec.message());
        return;
    }
    async_guard = register_callback(event, async_cb, this);
}

platform_context_t::~platform_context_t() {
    if (async_guard) {
        async_guard = {};
    }
}

void platform_context_t::notify() noexcept {
    if (async_guard) {
        ::SetEvent(async_guard.handle);
    }
}

auto platform_context_t::register_callback(handle_t handle, io_callback_t callback, void *data,
                                           close_handle_t close_cb) noexcept -> io_guard_t {
    auto log = utils::get_logger("fs");
    if (handle) {
        auto [_, inserted] = io_callbacks.emplace(handle, io_context_t(callback, data));
        if (inserted) {
            LOG_TRACE(log, "registered callback for handle {}", (void *)handle);
            handles.emplace_back(handle);
            return io_guard_t(this, close_cb ? close_cb : close_handle_cb, handle);
        } else {
            auto log = utils::get_logger("fs");
            LOG_WARN(log, "callback for the handle is already registered");
        }
    } else {
        LOG_WARN(log, "attempt to register callback for empty handle (ignored)");
    }
    return {};
}

auto platform_context_t::guard_handle(handle_t handle, close_handle_t close_cb) noexcept -> io_guard_t {
    return io_guard_t(this, close_cb ? close_cb : close_handle_cb, handle);
}

bool platform_context_t::wait_next_event() noexcept {
    auto has_events = false;
    if (handles.size()) {
        auto timeout = determine_wait_ms();
        auto sz = static_cast<DWORD>(handles.size());
        auto ptr = handles.data();
        auto r = ::WaitForMultipleObjects(sz, ptr, false, timeout);
        if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + sz) {
            has_events = true;
            for (int i = r - WAIT_OBJECT_0; i < WAIT_OBJECT_0 + sz; ++i) {
                auto h = ptr[i];
                if (::WaitForSingleObject(h, 0) == WAIT_OBJECT_0) {
                    auto it = io_callbacks.find(h);
                    if (it != io_callbacks.end()) {
                        auto &ctx = it->second;
                        ctx.cb(h, ctx.data);
                    } else {
                        auto log = utils::get_logger("fs");
                        LOG_WARN(log, "no callback for the handle {}", (void *)h);
                    }
                }
            }
        } else if (r == WAIT_TIMEOUT) {
            // NO-OP
        } else {
            auto ec = sys::error_code(::GetLastError(), sys::system_category());
            LOG_WARN(log, "WaitFor*Object failed: {}", ec.message());
        }
    }
    return has_events;
}

#endif
