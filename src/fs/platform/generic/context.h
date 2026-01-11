// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if !defined(SYNCSPIRIT_WATCHER_ANY)

#include "fs/platform/context_base.h"

namespace syncspirit::fs::platform::generic {

struct SYNCSPIRIT_API platform_context_t : context_base_t {
    using parent_t = context_base_t;
    using parent_t::parent_t;

    void notify() noexcept;
    void wait_next_event() noexcept;
};

} // namespace syncspirit::fs::platform::generic

#endif
