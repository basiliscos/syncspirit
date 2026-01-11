// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher_actor.h"

#include <boost/nowide/convert.hpp>

#if SYNCSPIRIT_WATCHER_INOTIFY
#include <sys/inotify.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#endif

using namespace syncspirit::fs;

using boost::nowide::narrow;

namespace {
namespace to {
struct context {};
} // namespace to
} // namespace

template <> auto &rotor::supervisor_t::access<to::context>() noexcept { return context; }

static constexpr auto actor_identity = "fs.watcher";

watch_actor_t::watch_actor_t(config_t &cfg) : parent_t{cfg} { log = utils::get_logger(actor_identity); }

void watch_actor_t::do_initialize(r::system_context_t *ctx) noexcept {
#if SYNCSPIRIT_WATCHER_INOTIFY
    inotify_lib = inotify_init();
    if (inotify_lib < 0) {
        LOG_ERROR(log, "inotify_init failed: {}", strerror(errno));
        return do_shutdown();
    } else {
        int flags = fcntl(inotify_lib, F_GETFL, 0);
        if (flags == -1) {
            LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
        } else {
            if (fcntl(inotify_lib, F_SETFL, flags | O_NONBLOCK) == -1) {
                LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
            }
        }
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
    root_guard = {};
    if (inotify_lib > 0) {
        close(inotify_lib);
    }
#endif
    parent_t::shutdown_finish();
}

void watch_actor_t::on_watch(message::watch_folder_t &message) noexcept {
    auto &p = message.payload;
    auto &path = p.path;
    LOG_TRACE(log, "on watch on '{}'", narrow(path.wstring()));
#if SYNCSPIRIT_WATCHER_INOTIFY
    auto generic_context = supervisor->access<to::context>();
    auto ctx = static_cast<fs::fs_context_t *>(generic_context);
    auto &path_str = path.native();
    auto fd = ::inotify_add_watch(inotify_lib, path_str.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE | IN_ATTRIB);
    if (fd <= 0) {
        LOG_ERROR(log, "cannot do inotify_add_watch(): {}", strerror(errno));
    } else {
        root_guard = ctx->register_callback(
            inotify_lib, [](auto, void *data) { reinterpret_cast<watch_actor_t *>(data)->inotify_callback(); }, this);
        p.ec = {};
    }
#endif
}

#if SYNCSPIRIT_WATCHER_INOTIFY
void watch_actor_t::inotify_callback() noexcept {
    char buffer[1024 * (sizeof(struct inotify_event) + NAME_MAX + 1)];
    int length = ::read(inotify_lib, buffer, sizeof(buffer));
    LOG_TRACE(log, "inotify callback, read result = {}", length);
    if (length < 0) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    // Process the events
    for (int i = 0; i < length;) {
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        if (event->len) {
            if (event->mask & IN_CREATE) {
                LOG_DEBUG(log, "File created: {}", event->name);
            }
            if (event->mask & IN_DELETE) {
                LOG_DEBUG(log, "File deleted: {}", event->name);
            }
            if (event->mask & IN_MODIFY) {
                LOG_DEBUG(log, "File modified: {}", event->name);
            }
            if (event->mask & IN_ATTRIB) {
                LOG_DEBUG(log, "File meta changed: {}", event->name);
            }
        }
        i += sizeof(struct inotify_event) + event->len;
    }
}
#endif
