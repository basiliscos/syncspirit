// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "cluster_supervisor.h"
#include "controller_actor.h"
#include "names.h"
#include "utils/error_code.h"
#include "hasher/hasher_proxy_actor.h"
#include "model/diff/peer/peer_state.h"
#include "../utils/format.hpp"

using namespace syncspirit::net;

cluster_supervisor_t::cluster_supervisor_t(cluster_supervisor_config_t &config)
    : ra::supervisor_asio_t{config}, bep_config{config.bep_config},
      hasher_threads{config.hasher_threads}, cluster{config.cluster} {
    log = utils::get_logger("net.cluster");
}

void cluster_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    ra::supervisor_asio_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("net::cluster", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&cluster_supervisor_t::on_model_update, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &) {
        create_actor<hasher::hasher_proxy_actor_t>()
            .timeout(init_timeout)
            .hasher_threads(hasher_threads)
            .name(net::names::hasher_proxy)
            .finish();
    });
}

void cluster_supervisor_t::on_start() noexcept {
    log->trace("{}, on_start", identity);
    ra::supervisor_asio_t::on_start();
}

void cluster_supervisor_t::shutdown_start() noexcept {
    log->trace("{}, shutdown_start", identity);
    ra::supervisor_asio_t::shutdown_start();
}

void cluster_supervisor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto cluster_supervisor_t::operator()(const model::diff::peer::peer_state_t &diff, void *) noexcept
    -> outcome::result<void> {
    if (!cluster->is_tainted() && diff.known) {
        auto peer = cluster->get_devices().by_sha256(diff.peer_id);
        LOG_TRACE(log, "{}, visiting peer_state_t, {}, state: {}", identity, peer->device_id(), (int)diff.state);
        if (diff.state == model::device_state_t::online) {
            /* auto addr = */
            create_actor<controller_actor_t>()
                .request_pool(bep_config.rx_buff_size)
                .outgoing_buffer_max(bep_config.tx_buff_limit)
                .blocks_max_requested(bep_config.blocks_max_requested)
                .timeout(init_timeout * 7 / 9)
                .peer(peer)
                .peer_addr(diff.peer_addr)
                .request_timeout(pt::milliseconds(bep_config.request_timeout))
                .cluster(cluster)
                .finish();
        }
    }
    return outcome::success();
}

void cluster_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    LOG_TRACE(log, "{}, on_child_shutdown: {}({})", identity, actor->get_identity(), actor->use_count());
    ra::supervisor_asio_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    if (state == r::state_t::OPERATIONAL) {
        log->debug("{}, on_child_shutdown, child {} termination: {}", identity, actor->get_identity(),
                   reason->message());
    }
}
