// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "bouncer_actor.h"
#include "net/names.h"

using namespace syncspirit::hasher;

void bouncer_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(net::names::bouncer, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.register_name(net::names::bouncer, get_address()); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) { p.subscribe_actor(&bouncer_actor_t::on_package); },
                                                    r::plugin::config_phase_t::PREINIT);
}

void bouncer_actor_t::on_package(message::package_t &message) noexcept {
    LOG_TRACE(log, "{}, on_package", identity);
    put(message.payload);
}
