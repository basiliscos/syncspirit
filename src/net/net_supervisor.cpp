#include "net_supervisor.h"
#include "global_discovery_actor.h"
#include "upnp_supervisor.h"
#include "acceptor_actor.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

net_supervisor_t::net_supervisor_t(ra::supervisor_asio_t *sup, ra::system_context_ptr_t ctx,
                                   const ra::supervisor_config_t &sup_cfg, const config::configuration_t &cfg)
    : ra::supervisor_asio_t(sup, ctx, sup_cfg), cfg{cfg}, guard(asio::make_work_guard(sup_cfg.strand->context())) {}

void net_supervisor_t::on_start(r::message_t<r::payload::start_actor_t> &msg) noexcept {
    spdlog::trace("net_supervisor_t::on_start");
    ra::supervisor_asio_t::on_start(msg);
    launch_acceptor();
    launch_upnp();
}

void net_supervisor_t::on_shutdown(r::message_t<r::payload::shutdown_request_t> &msg) noexcept {
    spdlog::trace("net_supervisor_t::on_shutdown");
    acceptor_addr.reset();
    ra::supervisor_asio_t::on_shutdown(msg);
}

void net_supervisor_t::launch_discovery() noexcept {
    create_actor<global_discovery_actor_t>(cfg.global_announce_config);
}

void net_supervisor_t::launch_acceptor() noexcept { acceptor_addr = create_actor<acceptor_actor_t>()->get_address(); }

void net_supervisor_t::launch_upnp() noexcept {
    using rt_config_t = upnp_supervisor_t::runtime_config_t;
    spdlog::trace("net_supervisor_t:: launching upnp supervisor");
    ra::system_context_ptr_t ctx(&get_asio_context());
    auto peers_acceptor_addr = get_address(); // temporally
    create_actor<upnp_supervisor_t>(ctx, ra::supervisor_asio_t::config, cfg.upnp_config,
                                    rt_config_t{acceptor_addr, peers_acceptor_addr});
}

void net_supervisor_t::confirm_shutdown() noexcept {
    spdlog::trace("net_supervisor_t::confirm_shutdown");
    ra::supervisor_asio_t::confirm_shutdown();
    guard.reset();
}
