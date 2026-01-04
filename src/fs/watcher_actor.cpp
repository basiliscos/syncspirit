// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher_actor.h"

#include <boost/nowide/convert.hpp>

#if SYNCSPIRIT_WATCHER_INOTIFY
#include <sys/inotify.h>
#include <string.h>
#include <unistd.h>
#endif

using namespace syncspirit::fs;

using boost::nowide::narrow;

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

void watch_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(actor_identity, false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) { p.subscribe_actor(&watch_actor_t::on_watch); });
}

void watch_actor_t::shutdown_finish() noexcept {
#if SYNCSPIRIT_WATCHER_INOTIFY
    if (inotify_lib > 0) {
        close(inotify_lib);
    }
#endif
    parent_t::shutdown_finish();
}

void watch_actor_t::on_watch(message::watch_folder_t &message) noexcept {
    auto &path = message.payload.path;
    LOG_TRACE(log, "on watch on '{}'", narrow(path.wstring()));
}
