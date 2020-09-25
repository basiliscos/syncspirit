#include "net_supervisor.h"
//#include "global_discovery_actor.h"
#include "upnp_actor.h"
#include "acceptor_actor.h"
#include "ssdp_actor.h"
#include "http_actor.h"
#include "resolver_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

net_supervisor_t::net_supervisor_t(net_supervisor_t::config_t& cfg):parent_t{cfg}, app_cfg{cfg.app_config} {
}

void net_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::coordinator, get_address());
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&net_supervisor_t::on_ssdp);
        p.subscribe_actor(&net_supervisor_t::on_port_mapping);
        launch_ssdp();
    });
}

void net_supervisor_t::on_child_shutdown(actor_base_t *actor, const std::error_code &ec) noexcept {
    parent_t::on_child_shutdown(actor, ec);
    spdlog::trace("net_supervisor_t::on_child_shutdown()");
    if (!ec && ssdp_addr && actor->get_address() == ssdp_addr) {
        return;
    }
    do_shutdown();
}


void net_supervisor_t::on_ssdp(message::ssdp_notification_t& message) noexcept {
    spdlog::trace("net_supervisor_t::on_ssdp");
    /* we no longer need it */
    send<r::message::shutdown_trigger_t>(get_address(), ssdp_addr);
    ssdp_addr.reset();

    auto& igd_url = message.payload.igd.location;
    auto timeout = shutdown_timeout / 2;
    auto io_timeout = shutdown_timeout / 3;
    create_actor<acceptor_actor_t>().timeout(timeout).local_address(message.payload.local_address).finish();
    create_actor<resolver_actor_t>().timeout(timeout).resolve_timeout(io_timeout).finish();
    create_actor<http_actor_t>().timeout(timeout).request_timeout(io_timeout).resolve_timeout(io_timeout).finish();
    create_actor<upnp_actor_t>().timeout(timeout)
            .descr_url(igd_url)
            .rx_buff_size(app_cfg.upnp_config.rx_buff_size)
            .external_port(app_cfg.upnp_config.external_port)
            .finish();
    return;
}

bool net_supervisor_t::launch_ssdp() noexcept {
    if (ssdp_attempts < 3) {
        auto timeout = shutdown_timeout / 2;
        ssdp_addr = create_actor<ssdp_actor_t>().timeout(timeout).max_wait(app_cfg.upnp_config.max_wait).finish()->get_address();
        ++ssdp_attempts;
        spdlog::trace("net_supervisor_t::launching ssdp, attempt #{}", ssdp_attempts);
        return true;
    }
    return false;
}

void net_supervisor_t::on_port_mapping(message::port_mapping_notification_t& message) noexcept {
    if (!message.payload.success) {
        spdlog::debug("net_supervisor_t::on_port_mapping shutting down self, unsuccesful port mapping");
        return do_shutdown();
    }
    std::abort();
}


#if 0
void net_supervisor_t::init_start() noexcept {
    subscribe(&net_supervisor_t::on_registration);
    registry_addr = create_actor<r::registry_t>(shutdown_timeout)->get_address();
    r::supervisor_t::init_start();
}

void net_supervisor_t::on_registration(r::message::registration_response_t &msg) noexcept {
    auto &ec = msg.payload.ec;
    if (ec) {
        spdlog::warn("net_supervisor_t::on_registration failure :: {}", ec.message());
        do_shutdown();
    } else {
        unsubscribe(&net_supervisor_t::on_registration);
        r::actor_base_t::init_start();
    }
}

void net_supervisor_t::on_start(r::message_t<r::payload::start_actor_t> &msg) noexcept {
    spdlog::trace("net_supervisor_t::on_start");
    ra::supervisor_asio_t::on_start(msg);
    launch_acceptor();
    launch_upnp();
}

void net_supervisor_t::on_shutdown(r::message::shutdown_request_t &msg) noexcept {
    spdlog::trace("net_supervisor_t::on_shutdown");
    ra::supervisor_asio_t::on_shutdown(msg);
}

void net_supervisor_t::launch_discovery() noexcept {
    create_actor<global_discovery_actor_t>(shutdown_timeout, cfg.global_announce_config);
}

void net_supervisor_t::launch_acceptor() noexcept { create_actor<acceptor_actor_t>(shutdown_timeout, registry_addr); }

void net_supervisor_t::launch_upnp() noexcept {
    spdlog::trace("net_supervisor_t:: launching upnp supervisor");
    ra::system_context_ptr_t ctx(&get_asio_context());
    auto peers_acceptor_addr = get_address(); // temporally
    auto timeout = r::pt::seconds{cfg.upnp_config.timeout};
    ra::supervisor_config_asio_t sup_config{timeout, strand};
    create_actor<upnp_supervisor_t>(timeout, sup_config, cfg.upnp_config, registry_addr);
}

void net_supervisor_t::shutdown_finish() noexcept {
    spdlog::trace("net_supervisor_t::shutdown_finish");
    guard.reset();
    ra::supervisor_asio_t::shutdown_finish();
}
#endif
