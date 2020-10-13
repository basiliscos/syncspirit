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
    if (ec) {
        auto &peer = res.payload.req->payload.request_payload->peer;
        spdlog::trace("peer_supervisor_t, peer {} wasn't discovered : {}", peer.value, ec.message());
    }
}
