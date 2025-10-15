// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "cluster_supervisor.h"
#include "controller_actor.h"
#include "names.h"
#include "utils/error_code.h"
#include "utils/format.hpp"
#include "model/diff/contact/peer_state.h"

using namespace syncspirit::net;

cluster_supervisor_t::cluster_supervisor_t(cluster_supervisor_config_t &config)
    : ra::supervisor_asio_t{config}, config{config.config}, cluster{config.cluster}, sequencer{config.sequencer} {}

void cluster_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    ra::supervisor_asio_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("net.cluster", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&cluster_supervisor_t::on_model_update, coordinator);
            }
        });
    });
}

void cluster_supervisor_t::on_start() noexcept {
    log->trace("on_start");
    ra::supervisor_asio_t::on_start();
}

void cluster_supervisor_t::shutdown_start() noexcept {
    log->trace("shutdown_start");
    ra::supervisor_asio_t::shutdown_start();
}

void cluster_supervisor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto cluster_supervisor_t::operator()(const model::diff::contact::peer_state_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (!cluster->is_tainted()) {
        auto peer = cluster->get_devices().by_sha256(diff.peer_id);
        auto conn_state = diff.state.get_connection_state();
        bool launch_controller = (peer->get_state() == diff.state && diff.state.is_online());
        LOG_TRACE(log, "visiting peer_state_t, {}, state: {}, has been online: {}, launch controller: {}",
                  peer->device_id(), (int)conn_state, diff.has_been_online, launch_controller);
        if (launch_controller) {
            auto &bep = config.bep_config;
            create_actor<controller_actor_t>()
                .cluster(cluster)
                .sequencer(sequencer)
                .timeout(init_timeout * 7 / 9)
                .peer(peer)
                .peer_addr(diff.peer_addr)
                .blocks_max_requested(bep.blocks_max_requested)
                .advances_per_iteration(bep.advances_per_iteration)
                .outgoing_buffer_max(bep.tx_buff_limit)
                .request_pool(bep.rx_buff_size)
                .default_path(config.default_location)
                .finish();
        }
    }
    return diff.visit_next(*this, custom);
}

void cluster_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    LOG_TRACE(log, "on_child_shutdown: {}({})", actor->get_identity(), actor->use_count());
    ra::supervisor_asio_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    if (state == r::state_t::OPERATIONAL) {
        log->debug("on_child_shutdown, child {} termination: {}", actor->get_identity(), reason->message());
    }
}
