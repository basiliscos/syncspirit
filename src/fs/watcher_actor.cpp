// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher_actor.h"

#if SYNCSPIRIT_WATCHER_INOTIFY
#include <sys/inotify.h>
#include <string.h>
#include <unistd.h>
#endif

using namespace syncspirit::fs;

static constexpr auto actor_identity = "fs.watcher";

watch_actor_t::watch_actor_t(config_t &cfg) : parent_t{cfg} { log = utils::get_logger(actor_identity); }

void watch_actor_t::do_initialize(r::system_context_t *ctx) noexcept {
#if SYNCSPIRIT_WATCHER_INOTIFY
    inotify_lib = inotify_init();
    if (inotify_lib < 0) {
        LOG_ERROR(log, "inotify_init failed: {}", strerror(errno));
        return do_shutdown();
    }
#endif
    parent_t::do_initialize(ctx);
}

void watch_actor_t::shutdown_finish() noexcept {
#if SYNCSPIRIT_WATCHER_INOTIFY
    if (inotify_lib > 0) {
        close(inotify_lib);
    }
#endif
    parent_t::shutdown_finish();
}
