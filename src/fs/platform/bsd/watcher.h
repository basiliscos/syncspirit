// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_KQUEUE

#include "fs/platform/unix/watcher.h"
#include "context.h"

namespace syncspirit::fs::platform::bsd {

struct SYNCSPIRIT_API watcher_t final : unix::watcher_t {
    using parent_t = unix::watcher_t;
    using parent_t::parent_t;

    sys::error_code unwatch_dir(int wd) noexcept override;
    outcome::result<int> watch_dir(std::string_view path) noexcept override;

    void kqueue_callback(int wd, std::uint32_t) noexcept;
};

} // namespace syncspirit::fs::platform::bsd

#endif