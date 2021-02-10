#include "../config/utils.h"
#include "../utils/error_code.h"
#include "net_supervisor.h"
#include "global_discovery_actor.h"
#include "local_discovery_actor.h"
#include "cluster_supervisor.h"
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

namespace fs = boost::filesystem;
using namespace syncspirit::net;

net_supervisor_t::net_supervisor_t(net_supervisor_t::config_t &cfg) : parent_t{cfg}, app_config{cfg.app_config} {
    auto &files_cfg = app_config.global_announce_config;
    auto result = utils::load_pair(files_cfg.cert_file.c_str(), files_cfg.key_file.c_str());
    if (!result) {
        spdlog::critical("cannot load certificate/key pair :: {}", result.error().message());
        throw result.error();
    }
    ssl_pair = std::move(result.value());
    auto device_id = model::device_id_t::from_cert(ssl_pair.cert_data);
    if (!device_id) {
        spdlog::critical("cannot create device_id from certificate");
        throw "cannot create device_id from certificate";
    }
    spdlog::info("{}, device name = {}, device id = {}", names::coordinator, app_config.device_name, device_id.value());

    auto cn = utils::get_common_name(ssl_pair.cert.get());
    if (!cn) {
        spdlog::critical("cannot get common name from certificate");
        throw "cannot get common name from certificate";
    }
    auto my_device = config::device_config_t{
        device_id.value().get_value(),
        app_config.device_name,
        config::compression_t::meta,
        cn.value(),
        false,
        false, /* auto accept */
        false, /* paused */
        false,
        {},
        {},
    };
    device = model::device_ptr_t(new model::device_t(my_device));
    update_devices();
}

void net_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(names::coordinator, false); });
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
        p.subscribe_actor(&net_supervisor_t::on_connect);
        p.subscribe_actor(&net_supervisor_t::on_disconnect);
        p.subscribe_actor(&net_supervisor_t::on_connection);
        p.subscribe_actor(&net_supervisor_t::on_auth);
        launch_cluster();
    });
}

void net_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    parent_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    spdlog::trace("{}, on_child_shutdown, {} due to {} ", identity, actor->get_identity(), reason->message());
    auto &child_addr = actor->get_address();
    if (ssdp_addr && child_addr == ssdp_addr) {
        ssdp_addr.reset();
        if (reason->ec != r::shutdown_code_t::normal && state == r::state_t::OPERATIONAL) {
            launch_ssdp();
            return;
        }
    }
    if (local_discovery_addr && child_addr == local_discovery_addr) {
        // ignore
        return;
    }
    if (peers_addr && peers_addr == child_addr) {
        peers_addr.reset();
    }
    if (cluster_addr && cluster_addr == child_addr) {
        cluster_addr.reset();
    }
    if (!peers_addr && !cluster_addr && cluster) {
        persist_data();
    }
    if (state == r::state_t::OPERATIONAL) {
        auto inner = r::make_error_code(r::shutdown_code_t::child_down);
        do_shutdown(make_error(inner, reason));
    }
}

void net_supervisor_t::on_child_init(actor_base_t *actor, const r::extended_error_ptr_t &ec) noexcept {
    parent_t::on_child_init(actor, ec);
    auto &child_addr = actor->get_address();
    if (!ec && cluster_addr && child_addr == cluster_addr) {
        spdlog::trace("{}, on_child_init, cluster has been loaded, laucning other children...", identity);
        launch_children();
    }
}

void net_supervisor_t::launch_cluster() noexcept {
    auto timeout = shutdown_timeout * 9 / 10;
    fs::path path(app_config.config_path);
    auto db_dir = path.append("mbdx-db");

    cluster = new model::cluster_t(device);
    create_actor<db_actor_t>().timeout(timeout).db_dir(db_dir.string()).device(device).finish();
    cluster_addr = create_actor<cluster_supervisor_t>()
                       .timeout(timeout)
                       .strand(strand)
                       .device(device)
                       .devices(&devices)
                       .cluster(cluster)
                       .folders(&app_config.folders)
                       .finish()
                       ->get_address();
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
                                   .device(device)
                                   .timeout(timeout)
                                   .finish()
                                   ->get_address();
    }
}

void net_supervisor_t::on_start() noexcept {
    spdlog::trace("{}, on_start", identity);
    parent_t::on_start();
    launch_ssdp();
}

void net_supervisor_t::on_ssdp(message::ssdp_notification_t &message) noexcept {
    spdlog::trace("{}, on_ssdp", identity);
    /* we no longer need it */
    auto ec = r::make_error_code(r::shutdown_code_t::normal);
    send<r::payload::shutdown_trigger_t>(get_address(), ssdp_addr, make_error(ec));

    auto &igd_url = message.payload.igd.location;
    auto timeout = shutdown_timeout * 9 / 10;
    upnp_addr = create_actor<upnp_actor_t>()
                    .timeout(timeout)
                    .descr_url(igd_url)
                    .rx_buff_size(app_config.upnp_config.rx_buff_size)
                    .external_port(app_config.upnp_config.external_port)
                    .finish()
                    ->get_address();
}

void net_supervisor_t::launch_ssdp() noexcept {
    auto &cfg = app_config.upnp_config;
    if (ssdp_attempts < cfg.discovery_attempts && !upnp_addr) {
        auto timeout = shutdown_timeout / 2;
        ssdp_addr = create_actor<ssdp_actor_t>().timeout(timeout).max_wait(cfg.max_wait).finish()->get_address();
        ++ssdp_attempts;
        spdlog::trace("{}, launching ssdp, attempt #{}", identity, ssdp_attempts);
    }
}

void net_supervisor_t::on_port_mapping(message::port_mapping_notification_t &message) noexcept {
    if (!message.payload.success) {
        spdlog::debug("{}, on_port_mapping shutting down self, unsuccesful port mapping", identity);
        auto ec = utils::make_error_code(utils::error_code::portmapping_failed);
        return do_shutdown(make_error(ec));
    }

    auto &cfg = app_config.global_announce_config;
    if (cfg.enabled) {
        auto global_device_id = model::device_id_t::from_string(cfg.device_id);
        if (!global_device_id) {
            spdlog::error("{}, on_port_mapping invalid global device id :: {} global discovery will not be used",
                          identity, cfg.device_id);
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
        auto &d = it.second;
        if (*d != *device) {
            discover(it.second);
        }
    }
}

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
            auto &urls = contact.value().uris;
            auto timeout = r::pt::milliseconds{app_config.bep_config.connect_timeout * (urls.size() + 1)};
            spdlog::warn("TODO: {}t::on_discovery, update last_seen", identity, req.device_id);
            request<payload::connect_request_t>(peers_addr, req.device_id, urls).send(timeout);
        } else {
            spdlog::trace("{}, on_discovery, no contact for {} has been discovered", identity,
                          req.device_id.get_short());
        }
    } else {
        spdlog::warn("{}, on_discovery, can't discover contacts for {} :: {}", req.device_id, identity, ec->message());
    }
}

void net_supervisor_t::on_discovery_notify(message::discovery_notify_t &message) noexcept {
    auto &device_id = message.payload.device_id;
    auto &peer_contact = message.payload.peer;
    if (peer_contact.has_value()) {
        auto &peer = peer_contact.value();
        auto &id = device_id.get_value();
        auto it = devices.find(id);
        if (it != devices.end()) {
            if (!it->second->online) {
                auto &urls = peer.uris;
                auto timeout = r::pt::milliseconds{app_config.bep_config.connect_timeout * (urls.size() + 1)};
                request<payload::connect_request_t>(peers_addr, device_id, peer.uris).send(timeout);
            }
        } else {
            bool notify = app_config.ignored_devices.count(id) == 0 && app_config.devices.count(id) == 0;
            if (notify) {
                using original_ptr_t = ui::payload::discovery_notification_t::message_ptr_t;
                send<ui::payload::discovery_notification_t>(address, original_ptr_t{&message});
            }
        }
    }
}

void net_supervisor_t::on_config_request(ui::message::config_request_t &message) noexcept {
    reply_to(message, app_config);
}

void net_supervisor_t::on_config_save(ui::message::config_save_request_t &message) noexcept {
    spdlog::trace("{}, on_config_save", identity);
    auto &cfg = message.payload.request_payload.config;
    auto result = save_config(cfg);
    if (result) {
        reply_to(message);
    } else {
        reply_with_error(message, make_error(result.error()));
    }
}

outcome::result<void> net_supervisor_t::save_config(const config::main_t &new_cfg) noexcept {
    auto path_tmp = new_cfg.config_path;
    path_tmp.append("syncspirit.toml.tmp");
    std::ofstream out(path_tmp.string(), out.binary);
    auto r = config::serialize(new_cfg, out);
    if (!r) {
        return r;
    }
    out.close();
    auto path = new_cfg.config_path;
    path.append("syncspirit.toml");
    sys::error_code ec;
    fs::rename(path_tmp, path, ec);
    if (ec) {
        return ec;
    }
    app_config = new_cfg;
    spdlog::warn("{}, on_config_save, apply changes", identity);
    update_devices();
    return outcome::success();
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
        auto &urls = device->static_addresses;
        auto timeout = r::pt::milliseconds{app_config.bep_config.connect_timeout * (urls.size() + 1)};
        request<payload::connect_request_t>(peers_addr, device->device_id, device->static_addresses).send(timeout);
    }
}

template <class> inline constexpr bool always_false_v = false;

void net_supervisor_t::on_connect(message::connect_response_t &message) noexcept {
    auto &ec = message.payload.ec;
    if (!ec) {
        /*
        auto &device_id = message.payload.res->peer_device_id;
        auto &device = devices.at(device_id.get_value());
        auto unknown = cluster->update(message.payload.res->cluster_config, devices);
        for (auto &folder : unknown) {
            send<ui::payload::new_folder_notify_t>(address, folder, device);
        }
        */
        auto &p = message.payload.res;
        send<payload::connect_notify_t>(cluster_addr, p->peer_addr, p->peer_device_id, p->cluster_config);
    } else {
        auto &payload = message.payload.req->payload.request_payload->payload;
        std::visit(
            [&](auto &&arg) {
                using P = payload::connect_request_t;
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, P::connect_info_t>) {
                    spdlog::debug("{}, on_connect, cannot establish connection to {} :: {}", identity, arg.device_id,
                                  ec->message());
                } else if constexpr (std::is_same_v<T, P::connected_info_t>) {
                    spdlog::debug("{}, on_connect, cannot authorize {} :: {}", identity, arg.remote, ec->message());
                } else {
                    static_assert(always_false_v<T>, "non-exhaustive visitor!");
                }
            },
            payload);
    }
}

void net_supervisor_t::on_disconnect(message::disconnect_notify_t &message) noexcept {
    auto &device_id = message.payload.peer_device_id;
    spdlog::info("{}, disconnected peer: {}", identity, device_id);
    send<payload::disconnect_notify_t>(cluster_addr, device_id, message.payload.peer_addr);
}

void net_supervisor_t::on_connection(message::connection_notify_t &message) noexcept {
    auto timeout = r::pt::milliseconds{app_config.bep_config.connect_timeout};
    auto &payload = message.payload;
    request<payload::connect_request_t>(peers_addr, std::move(payload.sock), payload.remote).send(timeout);
}

void net_supervisor_t::on_auth(message::auth_request_t &message) noexcept {
    auto &payload = message.payload.request_payload;
    auto &device_id = payload->peer_device_id;
    auto it = devices.find(device_id.get_value());
    model::device_ptr_t device;
    if (it == devices.end()) {
        if (app_config.ignored_devices.count(device_id.get_value()) == 0) {
            send<ui::payload::auth_notification_t>(address, &message);
        }
    } else {
        device = it->second;
        if (device->online) {
            spdlog::warn("{}, on_auth, {} requested authtorization, but there is already active connection?", identity,
                         device->device_id);
        } else {
            auto &cert_name = device->cert_name;
            if (cert_name) {
                bool ok = cert_name.value() == payload->cert_name;
                if (!ok) {
                    device.reset();
                }
            } else {
                cert_name = payload->cert_name;
            }
        }
    }
    using cluster_config_ptr_t = typename payload::auth_response_t::cluster_config_ptr_t;
    if (device) {
        device->mark_online(true);
        auto cluster_message = std::make_unique<cluster_config_ptr_t::element_type>(cluster->get());
        reply_to(message, std::move(cluster_message));
    } else {
        reply_to(message, cluster_config_ptr_t{});
    }
    spdlog::debug("{}, on_auth, {} requested authtorization. Result : {}", identity, device_id, (bool)device);
}

void net_supervisor_t::shutdown_start() noexcept {
    for (auto request_id : discovery_map) {
        send<message::discovery_cancel_t::payload_t>(global_discovery_addr, request_id);
    }
    parent_t::shutdown_start();
}

void net_supervisor_t::update_devices() noexcept {
    devices.clear();
    for (auto &it : app_config.devices) {
        auto device = model::device_ptr_t{new model::device_t(it.second)};
        devices.insert_or_assign(it.first, std::move(device));
    }
    devices.insert({device->device_id.get_value(), device});
}

void net_supervisor_t::persist_data() noexcept {
    auto config_copy = app_config;
    auto &devs = config_copy.devices;
    devs.clear();
    for (auto &[device_id, device] : devices) {
        if (device_id != this->device->device_id.get_value()) {
            devs.emplace(device_id, device->serialize());
        }
    }
    if (cluster) {
        config_copy.folders = cluster->serialize();
    }
    if (!(app_config == config_copy)) {
        spdlog::debug("{}, persist_data, saving config", identity);
        auto r = save_config(config_copy);
        if (!r) {
            spdlog::error("{}, persist_data, failed to save config:: {}", identity, r.error().message());
        }
    }
}
