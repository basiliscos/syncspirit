// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "syncspirit-config.h"
#include "utils/log.h"
#include <rotor.hpp>

namespace syncspirit::fs {

namespace r = rotor;

struct SYNCSPIRIT_API watch_actor_t : r::actor_base_t {
    using parent_t = r::actor_base_t;
    using config_t = parent_t::config_t;

    explicit watch_actor_t(config_t &cfg);
    void do_initialize(r::system_context_t *ctx) noexcept override;
    void shutdown_finish() noexcept override;

#if SYNCSPIRIT_WATCHER_INOTIFY
    int inotify_lib = -1;
#endif
    utils::logger_t log;
};

} // namespace syncspirit::fs
