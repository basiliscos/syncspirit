#include "peer_supervisor.h"
#include "names.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

peer_supervisor_t::peer_supervisor_t(peer_supervisor_config_t &cfg) : parent_t{cfg}, peer_list{cfg.peer_list} {
    discover_queue = peer_list;
}

void peer_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::coordinator, coordinator, false).link(); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&peer_supervisor_t::on_announce);
        p.subscribe_actor(&peer_supervisor_t::on_discovery);
    });
}

void peer_supervisor_t::discover_next_peer() noexcept {
    spdlog::trace("peer_supervisor_t::discover_next_peer");
    if (discover_queue.empty()) {
        spdlog::trace("peer_supervisor_t:: nobody to discover");
        return;
    }
    auto peer = discover_queue.front();
    auto timeout = shutdown_timeout * 9 / 10;
    request<payload::discovery_request_t>(coordinator, peer).send(timeout);

    discover_queue.pop_front();
}

void peer_supervisor_t::on_announce(message::announce_notification_t &) noexcept {
    spdlog::trace("peer_supervisor_t::on_announce()");
    discover_next_peer();
}

void peer_supervisor_t::on_discovery(message::discovery_response_t &res) noexcept {
    auto &ec = res.payload.ec;
    auto &device_id = res.payload.req->payload.request_payload->device_id.value;
    if (ec) {
        spdlog::warn("peer_supervisor_t, peer {} wasn't discovered : {}", device_id, ec.message());
        return discover_next_peer();
    }

    auto &peer_option = res.payload.res->peer;
    if (!peer_option) {
        spdlog::debug("peer_supervisor_t, peer {} not found", device_id);
        return discover_next_peer();
    }

    spdlog::trace("peer_supervisor_t, peer {} found, initiating connection", device_id);
}
