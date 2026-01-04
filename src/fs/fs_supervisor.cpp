// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "fs_supervisor.h"
#include "file_actor.h"
#include "fs_context.hpp"

#if SYNCSPIRIT_WATCHER_INOTIFY
#include <unistd.h>
#endif

using namespace syncspirit::fs;

fs_supervisor_t::fs_supervisor_t(config_t &cfg)
    : parent_t(cfg), fs_config{cfg.fs_config}, hasher_threads{cfg.hasher_threads} {
    rw_cache.reset(new file_cache_t(fs_config.mru_size));
}

void fs_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("fs.supervisor", false);
        log = utils::get_logger(identity);
    });
}

void fs_supervisor_t::enqueue(r::message_ptr_t message) noexcept {
    auto ctx = static_cast<fs_context_t *>(context);
    inbound_queue.push(message.detach());
#if SYNCSPIRIT_WATCHER_INOTIFY
    auto &flag = ctx->async_flag;
    if (!flag.load(std::memory_order_acquire)) {
        flag.store(true, std::memory_order_release);
        write(ctx->async_pipes[1], &(ctx->async_pipes[1]), 1);
    }
#elif SYNCSPIRIT_WATCHER_WIN32
    ::SetEvent(ctx->async_event);
#else
    std::lock_guard<std::mutex> lock(ctx->mutex);
    ctx->cv.notify_one();
#endif
}

void fs_supervisor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    parent_t::on_start();
    auto timeout = shutdown_timeout * 9 / 10;
    create_actor<file_actor_t>()
        .rw_cache(rw_cache)
        .concurrent_hashes(hasher_threads)
        .timeout(timeout)
        .escalate_failure()
        .finish();
}

void fs_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    parent_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    LOG_TRACE(log, "on_child_shutdown, '{}' due to {} ", actor->get_identity(), reason->message());
}
