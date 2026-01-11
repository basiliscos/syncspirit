// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher_base.h"

using namespace syncspirit::fs::platform;

static constexpr auto actor_identity = "fs.watcher";

watcher_base_t::watcher_base_t(config_t &cfg): parent_t{cfg} {
    log = utils::get_logger(actor_identity);
}

void watcher_base_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(actor_identity, false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) { p.subscribe_actor(&watcher_base_t::on_watch); });
}

void watcher_base_t::on_watch(message::watch_folder_t &) noexcept {
    LOG_WARN(log, "watching directory isn't supported by platform");
}
