// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "local_keeper.h"
#include "names.h"

using namespace syncspirit::net;

local_keeper_t::local_keeper_t(config_t &cfg): r::actor_base_t(cfg) {
}

void local_keeper_t::configure(r::plugin::plugin_base_t &plugin) noexcept  {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("net.local_keeper", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&local_keeper_t::on_model_update, coordinator);
            }
        });
    });
}

void local_keeper_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
}

void local_keeper_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    r::actor_base_t::shutdown_start();
}

void local_keeper_t::on_model_update(model::message::model_update_t &msg) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *msg.payload.diff;
    auto r = diff.visit(*this, const_cast<void *>(msg.payload.custom));
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}
