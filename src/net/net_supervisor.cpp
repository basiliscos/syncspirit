#include "net_supervisor.h"
#include "global_discovery_actor.h"
#include "upnp_actor.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

net_supervisor_t::net_supervisor_t(ra::supervisor_asio_t *sup, ra::system_context_ptr_t ctx,
                                   const ra::supervisor_config_t &sup_cfg, const config::configuration_t &cfg)
    : ra::supervisor_asio_t(sup, ctx, sup_cfg), cfg{cfg} {}

void net_supervisor_t::on_start(r::message_t<r::payload::start_actor_t> &msg) noexcept {
    spdlog::trace("net_supervisor_t::on_start");
    ra::supervisor_asio_t::on_start(msg);
    launch_upnp();
}

void net_supervisor_t::launch_discovery() noexcept {
    create_actor<global_discovery_actor_t>(cfg.global_announce_config);
}

void net_supervisor_t::launch_upnp() noexcept { create_actor<upnp_actor_t>(cfg.upnp_config); }
