// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "context.h"

#if SYNCSPIRIT_WATCHER_WIN32

using namespace syncspirit::fs::platform::windows;

platform_context_t::platform_context_t() noexcept {
    async_event = ::CreateEvent(nullptr, false, false, nullptr);
    if (!async_event) {
        LOG_CRITICAL(log, "cannot CreateEvent(): {}", ::GetLastError());
    }
}

platform_context_t::~platform_context_t() {
    if (async_event) {
        ::CloseHandle(async_event);
    }
}

void platform_context_t::notify() noexcept {
    if (async_event) {
        ::SetEvent(async_event);
    }
}

void platform_context_t::wait_next_event() noexcept {
    if (async_event) {
        auto timeout = determine_wait_ms();
        auto r = ::WaitForSingleObject(async_event, timeout);
        if (r == WAIT_OBJECT_0) {
            ::ResetEvent(async_event);
        } else if (r == WAIT_TIMEOUT) {
            // NO-OP
        } else {
            LOG_WARN(log, "WaitFor*Object failed: {}", ::GetLastError());
        }
    }
}

#endif
