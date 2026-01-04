// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <rotor/thread.hpp>
#include "syncspirit-export.h"
#include "syncspirit-config.h"
#include "utils/log.h"

#if SYNCSPIRIT_WATCHER_INOTIFY
#include <atomic>
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
    void wait_next_event() noexcept;

#if SYNCSPIRIT_WATCHER_INOTIFY
    int async_pipes[2];
    std::atomic_bool async_flag;
#elif SYNCSPIRIT_WATCHER_WIN32
    HANDLE async_event;
#endif
    utils::logger_t log;
};

} // namespace syncspirit::fs
