#include "peer_supervisor.h"
#include "peer_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

peer_supervisor_t::peer_supervisor_t(peer_supervisor_config_t &cfg)
    : parent_t{cfg}, device_name{cfg.device_name}, ssl_pair{*cfg.ssl_pair}, bep_config(cfg.bep_config) {}

void peer_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::peers, get_address());
        p.discover_name(names::coordinator, coordinator, false).link();
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&peer_supervisor_t::on_announce);
        p.subscribe_actor(&peer_supervisor_t::on_discovery);
        p.subscribe_actor(&peer_supervisor_t::on_discovery_notify);
    });
}

void peer_supervisor_t::on_child_shutdown(actor_base_t *actor, const std::error_code &ec) noexcept {
    auto *peer = static_cast<peer_actor_t *>(actor);
    spdlog::trace("peer_supervisor_t::on_child_shutdown, peer = {}", peer->device_id);
}

void peer_supervisor_t::on_start() noexcept {
    spdlog::trace("peer_supervisor_t::on_start (addr = {})", (void *)address.get());
    parent_t::on_start();
}

void peer_supervisor_t::discover_next_peer() noexcept {
    spdlog::trace("peer_supervisor_t::discover_next_peer");
    /*
        if (discover_queue.empty()) {
            spdlog::trace("peer_supervisor_t:: nobody to discover");
            return;
        }
        auto peer = discover_queue.front();
        auto timeout = shutdown_timeout * 9 / 10;
        request<payload::discovery_request_t>(coordinator, peer).send(timeout);

        discover_queue.pop_front();
    */
}

void peer_supervisor_t::on_announce(message::announce_notification_t &) noexcept {
    spdlog::trace("peer_supervisor_t::on_announce()");
    // discover_next_peer();
}

void peer_supervisor_t::on_discovery(message::discovery_response_t &res) noexcept {
    auto &ec = res.payload.ec;
    auto &device_id = res.payload.req->payload.request_payload->device_id;
    if (ec) {
        spdlog::warn("peer_supervisor_t, peer {} wasn't discovered : {}", device_id, ec.message());
        return discover_next_peer();
    }

    auto &peer_option = res.payload.res->peer;
    if (!peer_option) {
        spdlog::debug("peer_supervisor_t, peer {} not found", device_id);
        return discover_next_peer();
    }
    launch_peer(device_id, peer_option.value());
}

void peer_supervisor_t::launch_peer(const model::device_id_t &peer_device,
                                    const model::peer_contact_t &contact) noexcept {
    auto timeout = shutdown_timeout * 7 / 10;
    spdlog::trace("peer_supervisor_t, peer {} found, initiating connection", peer_device);
    create_actor<peer_actor_t>()
        .ssl_pair(&ssl_pair)
        .device_name(device_name)
        .peer_device_id(peer_device)
        .contact(contact)
        .bep_config(bep_config)
        .timeout(timeout)
        .finish();
}

void peer_supervisor_t::on_discovery_notify(message::discovery_notify_t &message) noexcept {
    auto &peer_device = message.payload.device_id;
    auto &peer_contact = message.payload.peer;
    launch_peer(peer_device, peer_contact.value());
}
