// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_INOTIFY

#include "fs/platform/unix/context.hpp"
#include "utils/log.h"
#include <sys/epoll.h>

namespace syncspirit::fs::platform::linux {

struct SYNCSPIRIT_API linux_backend_t {
    using events_t = std::vector<epoll_event>;

    linux_backend_t();
    bool initialize();
    void destroy();

    bool watch(int);
    void unwatch(int);

    bool poll(std::uint32_t timeout, unix::io_callbacks_map_t &callbacks);

    int monitor{-1};
    utils::logger_t log;
    events_t events;
};

namespace details {
using base_t = platform::unix::platform_context_t<linux_backend_t>;
}

struct SYNCSPIRIT_API platform_context_t : details::base_t {
    using parent_t = details::base_t;
    using parent_t::parent_t;
};

} // namespace syncspirit::fs::platform::linux

#endif
