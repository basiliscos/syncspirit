// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "bouncer_actor.h"
#include "net/names.h"

using namespace syncspirit::bouncer;

void bouncer_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(net::names::bouncer, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.register_name(net::names::bouncer, get_address()); }, r::plugin::config_phase_t::INITIALIZING);

    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) { subscribe(&bouncer_actor_t::on_package); },
                                                    r::plugin::config_phase_t::PREINIT);
}

void bouncer_actor_t::on_package(message::package_t &message) noexcept {
    LOG_TRACE(log, "on_package");
    supervisor->put(std::move(message.payload));
}

void bouncer_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    parent_t::shutdown_start();
}

void bouncer_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "shutdown_finish");
    parent_t::shutdown_finish();
}
