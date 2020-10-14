#include "net_supervisor.h"
#include "global_discovery_actor.h"
#include "upnp_actor.h"
#include "acceptor_actor.h"
#include "ssdp_actor.h"
#include "http_actor.h"
#include "resolver_actor.h"
#include "peer_supervisor.h"
#include "names.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

net_supervisor_t::net_supervisor_t(net_supervisor_t::config_t &cfg) : parent_t{cfg}, app_cfg{cfg.app_config} {
    auto &files_cfg = app_cfg.global_announce_config;
    auto result = ssl.load(files_cfg.cert_file.c_str(), files_cfg.key_file.c_str());
    if (!result) {
        spdlog::critical("cannot load certificate/key pair :: {}", result.error().message());
        throw result.error();
    }
}

void net_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.register_name(names::coordinator, get_address()); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&net_supervisor_t::on_ssdp);
        p.subscribe_actor(&net_supervisor_t::on_port_mapping);
        p.subscribe_actor(&net_supervisor_t::on_announce);
        p.subscribe_actor(&net_supervisor_t::on_discovery_req);
        p.subscribe_actor(&net_supervisor_t::on_discovery_res);
        launch_ssdp();
    });
}

void net_supervisor_t::on_child_shutdown(actor_base_t *actor, const std::error_code &ec) noexcept {
    parent_t::on_child_shutdown(actor, ec);
    spdlog::trace("net_supervisor_t::on_child_shutdown(), addr = {}", (void *)actor->get_address().get());
    if (!ec && ssdp_addr && actor->get_address() == ssdp_addr) {
        return;
    }
    do_shutdown();
}

void net_supervisor_t::on_ssdp(message::ssdp_notification_t &message) noexcept {
    spdlog::trace("net_supervisor_t::on_ssdp");
    /* we no longer need it */
    send<r::message::shutdown_trigger_t>(get_address(), ssdp_addr);
    ssdp_addr.reset();

    auto &igd_url = message.payload.igd.location;
    auto timeout = shutdown_timeout * 9 / 10;
    auto io_timeout = shutdown_timeout * 8 / 10;
    create_actor<acceptor_actor_t>().timeout(timeout).local_address(message.payload.local_address).finish();
    create_actor<resolver_actor_t>().timeout(timeout).resolve_timeout(io_timeout).finish();
    create_actor<http_actor_t>()
        .timeout(timeout)
        .request_timeout(io_timeout)
        .resolve_timeout(io_timeout)
        .registry_name(names::http10)
        .keep_alive(false)
        .finish();
    create_actor<upnp_actor_t>()
        .timeout(timeout)
        .descr_url(igd_url)
        .rx_buff_size(app_cfg.upnp_config.rx_buff_size)
        .external_port(app_cfg.upnp_config.external_port)
        .finish();

    // temporally hard-code
    peer_list_t peers;
    peers.push_back(model::device_id_t("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
    peers_addr =
        create_actor<peer_supervisor_t>().peer_list(peers).strand(strand).timeout(timeout).finish()->get_address();

    return;
}

bool net_supervisor_t::launch_ssdp() noexcept {
    if (ssdp_attempts < 3) {
        auto timeout = shutdown_timeout / 2;
        ssdp_addr = create_actor<ssdp_actor_t>()
                        .timeout(timeout)
                        .max_wait(app_cfg.upnp_config.max_wait)
                        .finish()
                        ->get_address();
        ++ssdp_attempts;
        spdlog::trace("net_supervisor_t::launching ssdp, attempt #{}", ssdp_attempts);
        return true;
    }
    return false;
}

void net_supervisor_t::on_port_mapping(message::port_mapping_notification_t &message) noexcept {
    if (!message.payload.success) {
        spdlog::debug("net_supervisor_t::on_port_mapping shutting down self, unsuccesful port mapping");
        return do_shutdown();
    }

    auto timeout = shutdown_timeout * 9 / 10;
    tcp::endpoint external_ep(message.payload.external_ip, app_cfg.upnp_config.external_port);
    auto &cfg = app_cfg.global_announce_config;
    global_discovery_addr = create_actor<global_discovery_actor_t>()
                                .timeout(timeout)
                                .endpoint(external_ep)
                                .ssl(&ssl)
                                .announce_url(cfg.announce_url)
                                .device_id(model::device_id_t(cfg.device_id))
                                .rx_buff_size(cfg.rx_buff_size)
                                .io_timeout(cfg.timeout)
                                .finish()
                                ->get_address();
}

void net_supervisor_t::on_announce(message::announce_notification_t &) noexcept {
    // this is needed to have multiple discovery services, i.e. global & local
    send<payload::announce_notification_t>(peers_addr, get_address());
}

void net_supervisor_t::on_discovery_req(message::discovery_request_t &req) noexcept {
    assert(global_discovery_addr);
    auto timeout = shutdown_timeout / 2;
    auto &peer = req.payload.request_payload->peer;
    auto req_id = request<payload::discovery_request_t>(global_discovery_addr, peer).send(timeout);
    discovery_map.emplace(req_id, &req);
}

void net_supervisor_t::on_discovery_res(message::discovery_response_t &res) noexcept {
    auto &ec = res.payload.ec;
    auto &req_id = res.payload.req->payload.id;
    auto &orig = discovery_map.at(req_id);
    if (ec) {
        reply_with_error(*orig, ec);
    } else {
        auto &urls = res.payload.res->uris;
        reply_to(*orig, std::move(urls));
    }
}
