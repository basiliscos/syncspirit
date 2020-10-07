#include "peer_supervisor.h"
#include "names.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

#if 0
peer_supervisor_t::peer_supervisor_t(peer_supervisor_config_t &cfg) : parent_t{cfg}, peer_list{cfg.peer_list} {
}

void peer_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::coordinator, coordinator, false).link();
    });
    /*
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&global_discovery_actor_t::on_announce); });
    */
}


void peer_supervisor_t::on_start() noexcept  {
    spdlog::trace("peer_supervisor_t::on_start");
    discover_queue = peer_list;
    discover_next_peer();
}


void peer_supervisor_t::discover_next_peer() noexcept {
    spdlog::trace("peer_supervisor_t::discover_next_peer");
    if (discover_queue.empty()) {
        spdlog::trace("peer_supervisor_t:: nobody to discover");
        return;
    }
    auto peer = discover_queue.front();
    discover_queue.pop_front();
    auto timeout = shutdown_timeout / 2;
    request<payload::discovery_request_t>(coordinator, peer).send(timeout);
}
#endif
