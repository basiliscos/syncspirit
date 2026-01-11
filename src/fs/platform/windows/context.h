// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_WIN32

#include "fs/platform/context_base.h"

#include <windows.h>

namespace syncspirit::fs::platform::windows {

struct SYNCSPIRIT_API platform_context_t : context_base_t {
    using parent_t = context_base_t;
    using parent_t::parent_t;

    platform_context_t() noexcept;
    ~platform_context_t();

    void notify() noexcept;
    void wait_next_event() noexcept;

    HANDLE async_event;
};

} // namespace syncspirit::fs::platform::windows

#endif
