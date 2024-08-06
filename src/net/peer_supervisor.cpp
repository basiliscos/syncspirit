// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "peer_supervisor.h"
#include "peer_actor.h"
#include "initiator_actor.h"
#include "names.h"
#include "../constants.h"
#include "utils/format.hpp"
#include "model/diff/contact/peer_state.h"
#include "model/diff/contact/connect_request.h"
#include "model/diff/contact/relay_connect_request.h"
#include "model/diff/contact/dial_request.h"
#include "model/misc/error_code.h"

using namespace syncspirit::net;

template <class> inline constexpr bool always_false_v = false;

peer_supervisor_t::peer_supervisor_t(peer_supervisor_config_t &cfg)
    : parent_t{cfg}, cluster{cfg.cluster}, device_name{cfg.device_name}, ssl_pair{*cfg.ssl_pair},
      bep_config(cfg.bep_config), relay_config{cfg.relay_config} {}

void peer_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(names::peer_supervisor, false);
        log = utils::get_logger(identity);
        addr_unknown = p.create_address();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::peer_supervisor, get_address());
        p.discover_name(names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&peer_supervisor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&peer_supervisor_t::on_contact_update, coordinator);
                plugin->subscribe_actor(&peer_supervisor_t::on_peer_ready);
                plugin->subscribe_actor(&peer_supervisor_t::on_connect);
                plugin->subscribe_actor(&peer_supervisor_t::on_connected, addr_unknown);
            }
        });
    });
}

void peer_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    using namespace model::diff;
    auto &reason = actor->get_shutdown_reason();
    LOG_TRACE(log, "on_child_shutdown, {} due to {} ", actor->get_identity(), reason->message());
    parent_t::on_child_shutdown(actor);
}

void peer_supervisor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    parent_t::on_start();
}

void peer_supervisor_t::on_model_update(model::message::model_update_t &msg) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *msg.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void peer_supervisor_t::on_contact_update(model::message::contact_update_t &msg) noexcept {
    LOG_TRACE(log, "on_contact_update");
    auto &diff = *msg.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void peer_supervisor_t::on_peer_ready(message::peer_connected_t &msg) noexcept {
    LOG_TRACE(log, "on_peer_ready");
    auto timeout = r::pt::milliseconds{bep_config.connect_timeout};
    auto &p = msg.payload;
    auto &d = p.peer_device_id;
    auto peer = cluster->get_devices().by_sha256(d.get_sha256());
    if (peer && peer->get_state() == model::device_state_t::online) {
        LOG_DEBUG(log, "peer '{}' is already online, ignoring request", d.get_short());
        return;
    }
    create_actor<peer_actor_t>()
        .transport(std::move(p.transport))
        .peer_device_id(d)
        .device_name(device_name)
        .bep_config(bep_config)
        .coordinator(coordinator)
        .peer_endpoint(p.remote_endpoint)
        .peer_proto(p.proto)
        .timeout(timeout)
        .cluster(cluster)
        .finish();
}

void peer_supervisor_t::on_connected(message::peer_connected_t &msg) noexcept {
    LOG_TRACE(log, "on_connected");
    auto &p = msg.payload;
    auto req = static_cast<message::connect_request_t *>(p.custom.get());
    reply_to(*req, std::move(p.transport), std::move(p.remote_endpoint));
}

void peer_supervisor_t::on_connect(message::connect_request_t &msg) noexcept {
    auto &p = msg.payload.request_payload;
    auto connect_timeout = r::pt::milliseconds{bep_config.connect_timeout};
    create_actor<initiator_actor_t>()
        .ssl_pair(&ssl_pair)
        .peer_device_id(p.device_id)
        .uris({p.uri})
        .custom(&msg)
        .router(*locality_leader)
        .sink(addr_unknown)
        .alpn(p.alpn)
        .init_timeout(connect_timeout * 2)
        .shutdown_timeout(connect_timeout)
        .finish();
}

auto peer_supervisor_t::operator()(const model::diff::contact::connect_request_t &diff, void *custom) noexcept
    -> outcome::result<void> {

    auto lock = std::unique_lock(diff.mutex);
    auto sock = std::move(*diff.sock);
    diff.sock.reset();

    auto timeout = r::pt::milliseconds{bep_config.connect_timeout};
    create_actor<initiator_actor_t>()
        .cluster(cluster)
        .router(*locality_leader)
        .sink(address)
        .ssl_pair(&ssl_pair)
        .sock(std::move(sock))
        .alpn(constants::protocol_name)
        .timeout(timeout)
        .finish();
    return diff.visit_next(*this, custom);
}

auto peer_supervisor_t::operator()(const model::diff::contact::relay_connect_request_t &diff, void *custom) noexcept
    -> outcome::result<void> {

    auto peer = cluster->get_devices().by_sha256(diff.peer.get_sha256());
    if (peer->get_state() == model::device_state_t::offline) {
        LOG_DEBUG(log, "initiating relay connection with {}", peer->device_id());
        auto timeout = r::pt::milliseconds{bep_config.connect_timeout};
        auto uri_str = fmt::format("tcp://{}", diff.relay);
        auto uri = utils::parse(uri_str);
        create_actor<initiator_actor_t>()
            .cluster(cluster)
            .router(*locality_leader)
            .sink(address)
            .ssl_pair(&ssl_pair)
            .peer_device_id(diff.peer)
            .uris(utils::uri_container_t{std::move(uri)})
            .relay_session(diff.session_key)
            .timeout(timeout)
            .finish();
    } else {
        LOG_DEBUG(log, "peer '{}' is not offline, dropping relay connection request", peer->device_id());
    }
    return diff.visit_next(*this, custom);
}

auto peer_supervisor_t::operator()(const model::diff::contact::dial_request_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &devices = cluster->get_devices();
    auto peer = devices.by_sha256(diff.peer_id);
    if (peer && peer->get_state() != model::device_state_t::online) {
        auto &uris = peer->get_uris();
        LOG_DEBUG(log, "initiating connection with {} ({} addresses)", peer->device_id(), uris.size());
        assert(uris.size());
        auto connect_timeout = r::pt::milliseconds{bep_config.connect_timeout};
        create_actor<initiator_actor_t>()
            .router(*locality_leader)
            .sink(address)
            .ssl_pair(&ssl_pair)
            .peer_device_id(peer->device_id())
            .uris(uris)
            .relay_enabled(relay_config.enabled)
            .cluster(cluster)
            .init_timeout(connect_timeout * (uris.size() + 1))
            .shutdown_timeout(connect_timeout)
            .finish();
    }
    return diff.visit_next(*this, custom);
}
