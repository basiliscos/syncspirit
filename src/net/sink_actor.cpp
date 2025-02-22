// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "sink_actor.h"
#include "names.h"

using namespace syncspirit::net;

void sink_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(names::sink, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::sink, get_address());
        p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&sink_actor_t::on_model_sink);
            }
        });
    });
}

void sink_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
}

void sink_actor_t::on_model_sink(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_sink");
    auto custom = const_cast<void *>(message.payload.custom);
    auto diff_ptr = reinterpret_cast<model::diff::cluster_diff_t *>(custom);
    if (diff_ptr) {
        auto diff = model::diff::cluster_diff_ptr_t(diff_ptr, false);
        send<model::payload::model_update_t>(coordinator, std::move(diff));
    }
}
