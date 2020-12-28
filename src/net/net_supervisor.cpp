#include "net_supervisor.h"
#include "global_discovery_actor.h"
#include "local_discovery_actor.h"
#include "upnp_actor.h"
#include "acceptor_actor.h"
#include "ssdp_actor.h"
#include "http_actor.h"
#include "resolver_actor.h"
#include "peer_supervisor.h"
#include "db_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>
#include <boost/filesystem.hpp>

using namespace syncspirit::net;
namespace fs = boost::filesystem;

net_supervisor_t::net_supervisor_t(net_supervisor_t::config_t &cfg) : parent_t{cfg}, app_config{cfg.app_config} {
    auto &files_cfg = app_config.global_announce_config;
    auto result = utils::load_pair(files_cfg.cert_file.c_str(), files_cfg.key_file.c_str());
    if (!result) {
        spdlog::critical("cannot load certificate/key pair :: {}", result.error().message());
        throw result.error();
    }
    auto device = model::device_id_t::from_cert(ssl_pair.cert_data);
    if (!device) {
        spdlog::critical("cannot create device_id from certificate");
        throw "cannot create device_id from certificate";
    }
    ssl_pair = std::move(result.value());
    device_id = std::move(device.value());
    spdlog::info("net_supervisor_t, device name = {},  device id = {}", app_config.device_name, device_id);

    for (auto &it : app_config.devices) {
        auto device = model::device_ptr_t{new model::device_t(it.second)};
        devices.emplace(it.first, std::move(device));
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
        p.subscribe_actor(&net_supervisor_t::on_discovery);
        p.subscribe_actor(&net_supervisor_t::on_discovery_notify);
        p.subscribe_actor(&net_supervisor_t::on_config_request);
        p.subscribe_actor(&net_supervisor_t::on_config_save);
        launch_children();
    });
}

void net_supervisor_t::on_child_shutdown(actor_base_t *actor, const std::error_code &ec) noexcept {
    parent_t::on_child_shutdown(actor, ec);
    spdlog::trace("net_supervisor_t::on_child_shutdown(), addr = {}", (void *)actor->get_address().get());
    auto &child_addr = actor->get_address();
    if (ssdp_addr && child_addr == ssdp_addr) {
        if (!ec && state == r::state_t::OPERATIONAL) {
            launch_ssdp();
            return;
        }
    }
    if (local_discovery_addr && child_addr == local_discovery_addr) {
        // ignore
        return;
    }
    if (state == r::state_t::OPERATIONAL) {
        do_shutdown();
    }
}

void net_supervisor_t::launch_children() noexcept {
    auto timeout = shutdown_timeout * 9 / 10;
    auto io_timeout = shutdown_timeout * 8 / 10;
    create_actor<acceptor_actor_t>().timeout(timeout).finish();
    create_actor<resolver_actor_t>().timeout(timeout).resolve_timeout(io_timeout).finish();
    create_actor<http_actor_t>()
        .timeout(timeout)
        .request_timeout(io_timeout)
        .resolve_timeout(io_timeout)
        .registry_name(names::http10)
        .keep_alive(false)
        .finish();

    fs::path path(app_config.config_path);
    auto db_dir = path.append("mbdx-db");

    create_actor<db_actor_t>().timeout(timeout).db_dir(db_dir.string()).finish();

    peers_addr = create_actor<peer_supervisor_t>()
                     .ssl_pair(&ssl_pair)
                     .device_name(app_config.device_name)
                     .strand(strand)
                     .timeout(timeout)
                     .bep_config(app_config.bep_config)
                     .finish()
                     ->get_address();

    if (app_config.local_announce_config.enabled) {
        auto &cfg = app_config.local_announce_config;
        local_discovery_addr = create_actor<local_discovery_actor_t>()
                                   .port(cfg.port)
                                   .frequency(cfg.frequency)
                                   .device_id(device_id)
                                   .timeout(timeout)
                                   .finish()
                                   ->get_address();
    }
}

void net_supervisor_t::on_start() noexcept {
    spdlog::trace("net_supervisor_t::on_start (addr = {})", (void *)address.get());
    parent_t::on_start();
    launch_ssdp();
}

void net_supervisor_t::on_ssdp(message::ssdp_notification_t &message) noexcept {
    spdlog::trace("net_supervisor_t::on_ssdp");
    /* we no longer need it */
    send<r::message::shutdown_trigger_t>(get_address(), ssdp_addr);
    ssdp_addr.reset();

    auto &igd_url = message.payload.igd.location;
    auto timeout = shutdown_timeout * 9 / 10;
    create_actor<upnp_actor_t>()
        .timeout(timeout)
        .descr_url(igd_url)
        .rx_buff_size(app_config.upnp_config.rx_buff_size)
        .external_port(app_config.upnp_config.external_port)
        .finish();
}

void net_supervisor_t::launch_ssdp() noexcept {
    auto &cfg = app_config.upnp_config;
    if (ssdp_attempts < cfg.discovery_attempts) {
        auto timeout = shutdown_timeout / 2;
        ssdp_addr = create_actor<ssdp_actor_t>().timeout(timeout).max_wait(cfg.max_wait).finish()->get_address();
        ++ssdp_attempts;
        spdlog::trace("net_supervisor_t::launching ssdp, attempt #{}", ssdp_attempts);
    }
}

void net_supervisor_t::on_port_mapping(message::port_mapping_notification_t &message) noexcept {
    if (!message.payload.success) {
        spdlog::debug("net_supervisor_t::on_port_mapping shutting down self, unsuccesful port mapping");
        return do_shutdown();
    }

    auto &cfg = app_config.global_announce_config;
    if (cfg.enabled) {
        auto global_device_id = model::device_id_t::from_string(cfg.device_id);
        if (!global_device_id) {
            spdlog::error(
                "net_supervisor_t::on_port_mapping invalid global device id :: {} global discovery will not be used",
                cfg.device_id);
            return;
        }
        auto timeout = shutdown_timeout * 9 / 10;
        tcp::endpoint external_ep(message.payload.external_ip, app_config.upnp_config.external_port);
        global_discovery_addr = create_actor<global_discovery_actor_t>()
                                    .timeout(timeout)
                                    .endpoint(external_ep)
                                    .ssl_pair(&ssl_pair)
                                    .announce_url(cfg.announce_url)
                                    .device_id(std::move(global_device_id.value()))
                                    .rx_buff_size(cfg.rx_buff_size)
                                    .io_timeout(cfg.timeout)
                                    .finish()
                                    ->get_address();
    }
}

void net_supervisor_t::on_announce(message::announce_notification_t &) noexcept {
    for (auto it : devices) {
        discover(it.second);
    }
}

#if 0
void net_supervisor_t::on_discovery_req(message::discovery_request_t &req) noexcept {
    if (global_discovery_addr) {
        assert(global_discovery_addr);
        auto timeout = shutdown_timeout / 2;
        auto &device_id = req.payload.request_payload->device_id;
        auto req_id = request<payload::discovery_request_t>(global_discovery_addr, device_id).send(timeout);
        discovery_map.emplace(req_id, &req);
    } else {
        auto ec = r::make_error_code(r::error_code_t::unknown_service);
        reply_with_error(req, ec);
    }
}
#endif

void net_supervisor_t::on_discovery(message::discovery_response_t &res) noexcept {
    auto &ec = res.payload.ec;
    auto &req_id = res.payload.req->payload.id;
    auto it = discovery_map.find(req_id);
    assert(it != discovery_map.end());
    discovery_map.erase(it);
    auto &req = *res.payload.req->payload.request_payload;
    if (!ec) {
        auto &contact = res.payload.res->peer;
        if (contact) {
            auto timeout = shutdown_timeout / 2;
            auto &urls = contact.value().uris;
            spdlog::warn("TODO: net_supervisor_t::on_discovery, update last_seen", req.device_id);
            request<payload::connect_request_t>(peers_addr, req.device_id, urls).send(timeout);
        } else {
            spdlog::trace("net_supervisor_t::on_discovery, no contact for {} has been discovered",
                          req.device_id.get_short());
        }
    } else {
        spdlog::warn("net_supervisor_t::on_discovery, can't discover contacts for {} :: {}", req.device_id,
                     ec.message());
    }
}

void net_supervisor_t::on_discovery_notify(message::discovery_notify_t &message) noexcept {
    auto &device_id = message.payload.device_id;
    auto &peer_contact = message.payload.peer;
    if (peer_contact.has_value()) {
        bool notify = app_config.ingored_devices.count(device_id.get_value()) == 0 &&
                      app_config.devices.count(device_id.get_value()) == 0;
        if (notify) {
            using original_ptr_t = ui::payload::discovery_notification_t::net_message_ptr_t;
            send<ui::payload::discovery_notification_t>(address, original_ptr_t{&message});
        }
    }
}

void net_supervisor_t::on_config_request(ui::message::config_request_t &message) noexcept {
    reply_to(message, app_config);
}

void net_supervisor_t::on_config_save(ui::message::config_save_request_t &message) noexcept {
    spdlog::trace("net_supervisor_t::on_config_save");
    auto &cfg = message.payload.request_payload.config;
    auto path_tmp = cfg.config_path;
    path_tmp.append("syncspirit.toml.tmp");
    std::ofstream out(path_tmp.string(), out.binary);
    auto r = config::serialize(cfg, out);
    if (!r) {
        reply_with_error(message, r.error());
        return;
    }
    out.close();
    auto path = cfg.config_path;
    path.append("syncspirit.toml");
    sys::error_code ec;
    fs::rename(path_tmp, path, ec);
    if (ec) {
        reply_with_error(message, r.error());
        return;
    }
    app_config = cfg;
    spdlog::warn("net_supervisor_t::on_config_save, apply changes");
    reply_to(message);
}

void net_supervisor_t::discover(model::device_ptr_t &device) noexcept {
    if (device->is_dynamic()) {
        if (global_discovery_addr) {
            assert(global_discovery_addr);
            auto timeout = shutdown_timeout / 2;
            auto &device_id = device->device_id;
            auto req_id = request<payload::discovery_request_t>(global_discovery_addr, device_id).send(timeout);
            discovery_map.emplace(req_id);
        }
    } else {
        auto timeout = shutdown_timeout / 2;
        request<payload::connect_request_t>(peers_addr, device->device_id, device->static_addresses).send(timeout);
    }
}

void net_supervisor_t::shutdown_start() noexcept {
    for (auto request_id : discovery_map) {
        send<message::discovery_cancel_t::payload_t>(global_discovery_addr, request_id);
    }
    parent_t::shutdown_start();
}
