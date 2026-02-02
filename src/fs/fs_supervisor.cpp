// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "fs_supervisor.h"
#include "file_actor.h"
#include "fs_context.h"
#include "watcher_actor.h"

#if SYNCSPIRIT_WATCHER_INOTIFY
#include <unistd.h>
#endif

using namespace syncspirit::fs;

fs_supervisor_t::fs_supervisor_t(config_t &cfg)
    : parent_t(cfg), fs_config{cfg.fs_config}, hasher_threads{cfg.hasher_threads} {}

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
    ctx->notify();
}

void fs_supervisor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    parent_t::on_start();
    launch_children();
}

void fs_supervisor_t::launch_children() noexcept {
    auto retension = pt::milliseconds{fs_config.retension_timeout};
    updates_mediator.reset(new updates_mediator_t(retension * 2));
    auto timeout = shutdown_timeout * 9 / 10;
    create_actor<file_actor_t>().concurrent_hashes(hasher_threads).timeout(timeout).escalate_failure().finish();
    create_actor<watch_actor_t>()
        .timeout(timeout)
        .change_retension(retension)
        .updates_mediator(updates_mediator)
        .finish();
}

void fs_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    parent_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    LOG_TRACE(log, "on_child_shutdown, '{}' due to {} ", actor->get_identity(), reason->message());
}
