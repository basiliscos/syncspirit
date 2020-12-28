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
        p.discover_name(names::coordinator, coordinator, true).link(false);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&peer_supervisor_t::on_connect); });
}

void peer_supervisor_t::on_child_shutdown(actor_base_t *actor, const std::error_code &ec) noexcept {
    auto& peer_id = addr2id.at(actor->get_address());
    spdlog::trace("peer_supervisor_t::on_child_shutdown, peer: {}, reason: {}", peer_id, ec.message());
    parent_t::on_child_shutdown(actor, ec);
}

void peer_supervisor_t::on_start() noexcept {
    spdlog::trace("peer_supervisor_t::on_start (addr = {})", (void *)address.get());
    parent_t::on_start();
}

void peer_supervisor_t::on_connect(message::connect_request_t &msg) noexcept {
    auto &payload = msg.payload.request_payload;
    auto timeout = shutdown_timeout * 7 / 10;
    auto &peer_id = payload.device_id;
    auto &uris = payload.uris;
    spdlog::trace("peer_supervisor_t::on_connect, initiating connection with {}", peer_id);
    auto peer_addr = create_actor<peer_actor_t>()
        .ssl_pair(&ssl_pair)
        .device_name(device_name)
        .peer_device_id(peer_id)
        .uris(uris)
        .bep_config(bep_config)
        .timeout(timeout)
        .finish()
        ->get_address();
    addr2id.emplace(peer_addr, peer_id);
    id2addr.emplace(peer_id, peer_addr);
}
