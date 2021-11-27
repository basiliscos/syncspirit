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
#include "dialer_actor.h"
#include "db_actor.h"
#include "names.h"
#include <boost/filesystem.hpp>
#include <algorithm>
#include <ctime>

namespace bfs = boost::filesystem;
using namespace syncspirit::net;

net_supervisor_t::net_supervisor_t(net_supervisor_t::config_t &cfg) : parent_t{cfg}, cluster_copies{cfg.cluster_copies}, app_config{cfg.app_config} {
    seed = (size_t) std::time(nullptr);
    log = utils::get_logger("net.coordinator");
    auto &files_cfg = app_config.global_announce_config;
    auto result = utils::load_pair(files_cfg.cert_file.c_str(), files_cfg.key_file.c_str());
    if (!result) {
        LOG_CRITICAL(log, "cannot load certificate/key pair :: {}", result.error().message());
        throw result.error();
    }
    ssl_pair = std::move(result.value());
    auto device_id = model::device_id_t::from_cert(ssl_pair.cert_data);
    if (!device_id) {
        LOG_CRITICAL(log, "cannot create device_id from certificate");
        throw "cannot create device_id from certificate";
    }
    LOG_INFO(log, "{}, device name = {}, device id = {}", names::coordinator, app_config.device_name,
             device_id.value());

    auto cn = utils::get_common_name(ssl_pair.cert.get());
    if (!cn) {
        LOG_CRITICAL(log, "cannot get common name from certificate");
        throw "cannot get common name from certificate";
    }
    auto device_opt = model::device_t::create(device_id.value(), app_config.device_name, cn.value());
    if (!device_opt) {
        LOG_CRITICAL(log, "cannot get common name from certificate");
        throw "cannot get common name from certificate";
    }
    auto device = model::device_ptr_t();
    device = new model::local_device_t(device_id.value(), app_config.device_name, cn.value());
    cluster = new model::cluster_t(device, seed);
}

void net_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(names::coordinator, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.register_name(names::coordinator, get_address()); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&net_supervisor_t::on_ssdp);
        p.subscribe_actor(&net_supervisor_t::on_port_mapping);
#if 0
        p.subscribe_actor(&net_supervisor_t::on_discovery_notify);
        p.subscribe_actor(&net_supervisor_t::on_config_request);
        p.subscribe_actor(&net_supervisor_t::on_config_save);
        p.subscribe_actor(&net_supervisor_t::on_connect);
        p.subscribe_actor(&net_supervisor_t::on_dial_ready);
        p.subscribe_actor(&net_supervisor_t::on_connection);
#endif
        p.subscribe_actor(&net_supervisor_t::on_model_update);
        p.subscribe_actor(&net_supervisor_t::on_block_update);
        p.subscribe_actor(&net_supervisor_t::on_load_cluster);
        p.subscribe_actor(&net_supervisor_t::on_model_request);
        launch_early();
    });
}

void net_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    parent_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    LOG_TRACE(log, "{}, on_child_shutdown, '{}' due to {} ", identity, actor->get_identity(), reason->message());
    auto &child_addr = actor->get_address();
    if (ssdp_addr && child_addr == ssdp_addr) {
#if 0
        ssdp_addr.reset();
        auto &ec = reason->root()->ec;
        auto ssdp = (ec != r::shutdown_code_t::normal && state == r::state_t::OPERATIONAL);
        if (ssdp) {
            launch_ssdp();
        }
#endif
        return;
    }
    if (local_discovery_addr && child_addr == local_discovery_addr) {
        local_discovery_addr.reset();
        return;
    }
    if (upnp_addr && upnp_addr == child_addr) {
        upnp_addr.reset();
        return;
    }
    if (peers_addr && peers_addr == child_addr) {
        peers_addr.reset();
    }
    if (cluster_addr && cluster_addr == child_addr) {
        cluster_addr.reset();
    }
/*
    if (!peers_addr && !cluster_addr && cluster) {
        LOG_WARN(log, "{}, TODO, persist_data", identity);
    }
*/
    if (state == r::state_t::OPERATIONAL) {
        auto inner = r::make_error_code(r::shutdown_code_t::child_down);
        do_shutdown(make_error(inner, reason));
    }
}

void net_supervisor_t::on_child_init(actor_base_t *actor, const r::extended_error_ptr_t &ec) noexcept {
    parent_t::on_child_init(actor, ec);
    auto &child_addr = actor->get_address();
    if (!ec && db_addr && child_addr == db_addr) {
        LOG_TRACE(log, "{}, on_child_init, db has been launched, let's load it...", identity);
        load_db();
    }
}

void net_supervisor_t::launch_early() noexcept {
    auto timeout = shutdown_timeout * 9 / 10;
    bfs::path path(app_config.config_path);
    auto db_dir = path.append("mbdx-db");
    db_addr = create_actor<db_actor_t>()
            .timeout(timeout).db_dir(db_dir.string())
            .cluster(cluster)
            .finish()
            ->get_address();
}

void net_supervisor_t::load_db() noexcept {
    auto timeout = init_timeout * 9 / 10;
    request<payload::load_cluster_request_t>(db_addr).send(timeout);
}

void net_supervisor_t::seed_model() noexcept {
    send<payload::model_update_t>(address, std::move(load_diff), nullptr);
}

void net_supervisor_t::on_load_cluster(message::load_cluster_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    if (ee) {
        LOG_ERROR(log, "{}, cannot load clusted : {}", identity, ee->message());
        return do_shutdown(ee);
    }
    LOG_TRACE(log, "{}, on_load_cluster", identity);
    load_diff = std::move(message.payload.res.diff);
    if (cluster_copies == 0) {
        seed_model();
    }
}

void net_supervisor_t::on_model_request(message::model_request_t &message) noexcept {
    --cluster_copies;
    LOG_TRACE(log, "{}, on_cluster_seed, left = {}", identity, cluster_copies);
    auto my_device = cluster->get_device();
    auto device = model::device_ptr_t();
    device = new model::local_device_t(my_device->device_id(), app_config.device_name, "");
    auto cluster_copy = new model::cluster_t(device, seed);
    reply_to(message, std::move(cluster_copy));
    if (cluster_copies == 0 && load_diff) {
        seed_model();
    }
}

void net_supervisor_t::on_model_update(message::model_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto& diff = *message.payload.diff;
    auto r = diff.apply(*cluster);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
    r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void net_supervisor_t::on_block_update(message::block_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_block_update", identity);
    auto& diff = *message.payload.diff;
    auto r = diff.apply(*cluster);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto net_supervisor_t::operator()(const model::diff::load::load_cluster_t &) noexcept -> outcome::result<void> {
    if (!cluster->is_tainted()) {

        auto& ignored_devices = cluster->get_ignored_devices();
        auto& ignored_folders = cluster->get_ignored_folders();
        auto& devices = cluster->get_devices();
        LOG_DEBUG(log,
                  "{}, load cluster. devices = {}, ignored devices = {}, ignored folders = {}, folders = {}, blocks = {}",
                  identity, devices.size(), ignored_devices.size(), ignored_folders.size(), cluster->get_folders().size(),
                  cluster->get_blocks().size());

        cluster_addr = create_actor<cluster_supervisor_t>()
                           .timeout(shutdown_timeout * 9 / 10)
                           .strand(strand)
                           .cluster(cluster)
                           .bep_config(app_config.bep_config)
                           .hasher_threads(app_config.hasher_threads)
                           .finish()
                           ->get_address();

        launch_ssdp();
        launch_net();
    }
    return outcome::success();
}


#if 0
void net_supervisor_t::on_cluster_ready(message::cluster_ready_notify_t &message) noexcept {
    LOG_TRACE(log, "{}, on_cluster_ready", identity);
    auto &ee = message.payload.ee;
    if (ee) {
        LOG_CRITICAL(log, "{}, cluster is not ready: {}, ", ee->message());
        return do_shutdown(ee);
    }
    launch_net();
}
#endif

void net_supervisor_t::launch_net() noexcept {
    auto timeout = shutdown_timeout * 9 / 10;
    LOG_INFO(log, "{}, launching network services", identity);

    create_actor<acceptor_actor_t>().timeout(timeout).finish();
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
                                   .device(cluster->get_device())
                                   .timeout(timeout)
                                   .finish()
                                   ->get_address();
    }
}

void net_supervisor_t::launch_upnp() noexcept {
    LOG_DEBUG(log, "{}, launching upnp", identity);
    assert(igd_location);
    auto timeout = shutdown_timeout * 9 / 10;

    upnp_addr = create_actor<upnp_actor_t>()
                    .timeout(timeout)
                    .descr_url(igd_location)
                    .rx_buff_size(app_config.upnp_config.rx_buff_size)
                    .external_port(app_config.upnp_config.external_port)
                    .finish()
                    ->get_address();
}

void net_supervisor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    parent_t::on_start();
    auto timeout = shutdown_timeout * 9 / 10;
    auto io_timeout = shutdown_timeout * 8 / 10;

    create_actor<resolver_actor_t>().timeout(timeout).resolve_timeout(io_timeout).finish();
    create_actor<http_actor_t>()
        .timeout(timeout)
        .request_timeout(io_timeout)
        .resolve_timeout(io_timeout)
        .registry_name(names::http10)
        .keep_alive(false)
        .finish();
}

void net_supervisor_t::on_ssdp(message::ssdp_notification_t &message) noexcept {
    LOG_TRACE(log, "{}, on_ssdp", identity);
    /* we no longer need it */
    auto ec = r::make_error_code(r::shutdown_code_t::normal);
    send<r::payload::shutdown_trigger_t>(get_address(), ssdp_addr, make_error(ec));

    igd_location = message.payload.igd.location;
    launch_upnp();
}

void net_supervisor_t::launch_ssdp() noexcept {
    auto &cfg = app_config.upnp_config;
    if (ssdp_attempts < cfg.discovery_attempts && !upnp_addr) {
        auto timeout = shutdown_timeout / 2;
        ssdp_addr = create_actor<ssdp_actor_t>().timeout(timeout).max_wait(cfg.max_wait).finish()->get_address();
        ++ssdp_attempts;
        LOG_TRACE(log, "{}, launching ssdp, attempt #{}", identity, ssdp_attempts);
    }
}

void net_supervisor_t::on_port_mapping(message::port_mapping_notification_t &message) noexcept {
    if (state != r::state_t::OPERATIONAL) {
        return;
    }

    asio::ip::address ip;
    if (!message.payload.success) {
        LOG_INFO(log, "{}, on_port_mapping, unsuccesful port mapping", identity);
        // auto ec = utils::make_error_code(utils::error_code_t::portmapping_failed);
        // Return do_shutdown(make_error(ec));
    } else {
        ip = message.payload.external_ip;
    }

    auto &gcfg = app_config.global_announce_config;
    if (gcfg.enabled) {
        auto global_device_id = model::device_id_t::from_string(gcfg.device_id);
        if (!global_device_id) {
            LOG_ERROR(log, "{}, on_port_mapping invalid global device id :: {} global discovery will not be used",
                      identity, gcfg.device_id);
            return;
        }
        auto timeout = shutdown_timeout * 9 / 10;
        auto port = app_config.upnp_config.external_port;
        tcp::endpoint external_ep(ip, port);
        create_actor<global_discovery_actor_t>()
            .timeout(timeout)
            .endpoint(external_ep)
            .ssl_pair(&ssl_pair)
            .announce_url(gcfg.announce_url)
            .device_id(std::move(global_device_id.value()))
            .rx_buff_size(gcfg.rx_buff_size)
            .io_timeout(gcfg.timeout)
            .finish();
        auto dcfg = app_config.dialer_config;
        if (dcfg.enabled) {
            create_actor<dialer_actor_t>()
                .timeout(timeout)
                .dialer_config(dcfg)
                .bep_config(app_config.bep_config)
                .cluster(cluster)
                .finish();
        }
    }
}

#if 0
void net_supervisor_t::on_discovery_notify(message::discovery_notify_t &message) noexcept {
    auto &device_id = message.payload.device_id;
    auto &peer_contact = message.payload.peer;
    if (peer_contact.has_value()) {
        auto &peer = peer_contact.value();
        auto &id = device_id.get_sha256();
        auto target_device = devices.by_id(id);
        if (target_device) {
            if (!target_device->is_online()) {
                dial_peer(device_id, peer.uris);
            }
        } else {
            auto ignored_device = ignored_devices.by_key(id);
            if (!ignored_device && id != this->device->device_id.get_sha256()) {
                using original_ptr_t = ui::payload::discovery_notification_t::message_ptr_t;
                send<ui::payload::discovery_notification_t>(address, original_ptr_t{&message});
            } else {
                LOG_INFO(log, "{}, on_discovery_notify, ignoring {}", identity, *ignored_device);
            }
        }
    }
}

void net_supervisor_t::on_dial_ready(message::dial_ready_notify_t &message) noexcept {
    auto &payload = message.payload;
    dial_peer(payload.device_id, payload.uris);
}

void net_supervisor_t::dial_peer(const model::device_id_t &peer_device_id,
                                 const utils::uri_container_t &uris) noexcept {
    auto timeout = r::pt::milliseconds{app_config.bep_config.connect_timeout * (uris.size() + 1)};
    request<payload::connect_request_t>(peers_addr, peer_device_id, uris).send(timeout);
}

void net_supervisor_t::on_config_request(ui::message::config_request_t &message) noexcept {
    reply_to(message, app_config);
}

void net_supervisor_t::on_config_save(ui::message::config_save_request_t &message) noexcept {
    LOG_TRACE(log, "{}, on_config_save", identity);
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
    bfs::rename(path_tmp, path, ec);
    if (ec) {
        return ec;
    }
    app_config = new_cfg;
    LOG_WARN(log, "{}, on_config_save, apply changes", identity);
    // update_devices();
    return outcome::success();
}

template <class> inline constexpr bool always_false_v = false;

void net_supervisor_t::on_connect(message::connect_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    if (!ee) {
        auto &p = message.payload.res;
        send<payload::connect_notify_t>(cluster_addr, p->peer_addr, p->peer_device_id, std::move(p->cluster_config));
    } else {
        auto &payload = message.payload.req->payload.request_payload->payload;
        std::visit(
            [&](auto &&arg) {
                using P = payload::connect_request_t;
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, P::connect_info_t>) {
                    LOG_DEBUG(log, "{}, on_connect, cannot establish connection to {} :: {}", identity, arg.device_id,
                              ee->message());
                } else if constexpr (std::is_same_v<T, P::connected_info_t>) {
                    LOG_DEBUG(log, "{}, on_connect, cannot authorize {} :: {}", identity, arg.remote, ee->message());
                } else {
                    static_assert(always_false_v<T>, "non-exhaustive visitor!");
                }
            },
            payload);
    }
}

void net_supervisor_t::on_disconnect(message::disconnect_notify_t &message) noexcept {
    auto &device_id = message.payload.peer_device_id;
    LOG_INFO(log, "{}, disconnected peer: {}", identity, device_id);
    if (cluster_addr) {
        send<payload::disconnect_notify_t>(cluster_addr, device_id, message.payload.peer_addr);
    }
}

void net_supervisor_t::on_connection(message::connection_notify_t &message) noexcept {
    auto timeout = r::pt::milliseconds{app_config.bep_config.connect_timeout};
    auto &payload = message.payload;
    request<payload::connect_request_t>(peers_addr, std::move(payload.sock), payload.remote).send(timeout);
}

void net_supervisor_t::on_auth(message::auth_request_t &message) noexcept {
    auto &payload = message.payload.request_payload;
    auto &device_id = payload->peer_device_id;
    auto device = devices.by_id(device_id.get_sha256());
    if (!device) {
        if (!ignored_devices.by_key(device_id.get_sha256())) {
            send<ui::payload::auth_notification_t>(address, &message);
        } else {
            LOG_INFO(log, "{}, on_auth, ignoring {}", identity, device_id);
        }

    } else {
        if (device->is_online()) {
            LOG_WARN(log, "{}, on_auth, {} requested authtorization, but there is already active connection?", identity,
                     device->device_id);
        } else {
            auto &cert_name = device->cert_name;
            if (cert_name && !cert_name.value().empty()) {
                bool ok = cert_name.value() == payload->cert_name;
                if (!ok) {
                    device.reset();
                }
            } else {
                cert_name = payload->cert_name;
            }
        }
    }
    using cluster_config_t = std::decay_t<decltype(cluster->get(device))>;
    if (device) {
        device->mark_online(true);
        auto cluster_config = cluster->get(device);
        auto cluster_message = std::make_unique<cluster_config_t>(std::move(cluster_config));
        reply_to(message, std::move(cluster_message));
    } else {
        reply_to(message, std::unique_ptr<cluster_config_t>{});
    }
    LOG_DEBUG(log, "{}, on_auth, {} requested authtorization. Result : {}", identity, device_id, (bool)device);
}

void net_supervisor_t::on_ignore_device(ui::message::ignore_device_request_t &message) noexcept {
    ignore_device_req.reset(&message);
    auto &peer = message.payload.request_payload.device;
    LOG_TRACE(log, "{}, on_ignore_device, {}", identity, *peer);
    assert(!ignored_devices.by_key(peer->get_sha256()));
    request<payload::store_ignored_device_request_t>(db_addr, peer).send(init_timeout / 2);
}

void net_supervisor_t::on_ignore_folder(ui::message::ignore_folder_request_t &message) noexcept {
    ingored_folder_requests.emplace_back(&message);
    auto &folder = message.payload.request_payload.folder;
    LOG_TRACE(log, "{}, on_ignore_folder, {}", identity, folder->id);
    assert(!ignored_folders.by_key(folder->id));
    request<payload::store_ignored_folder_request_t>(db_addr, folder).send(init_timeout / 2);
}

void net_supervisor_t::on_store_ignored_device(message::store_ignored_device_response_t &message) noexcept {
    auto &peer = message.payload.req->payload.request_payload.device;
    LOG_TRACE(log, "{}, on_store_ingnored_device, {}", identity, *peer);
    assert(ignore_device_req);
    auto &ee = message.payload.ee;
    if (ee) {
        reply_with_error(*ignore_device_req, ee);
    } else {
        LOG_DEBUG(log, "{}, ignoring device {}", identity, *peer);
        ignored_devices.put(peer);
        reply_to(*ignore_device_req);
    }
    ignore_device_req.reset();
}

void net_supervisor_t::on_store_ignored_folder(message::store_ignored_folder_response_t &message) noexcept {
    auto &folder = message.payload.req->payload.request_payload.folder;
    LOG_TRACE(log, "{}, on_store_ingnored_folder, {}", identity, folder->id);
    auto predicate = [&](ignore_folder_req_t &req) { return req->payload.request_payload.folder == folder; };
    auto it = std::find_if(ingored_folder_requests.begin(), ingored_folder_requests.end(), predicate);
    assert(it != ingored_folder_requests.end());
    auto &ee = message.payload.ee;
    if (ee) {
        reply_with_error(**it, ee);
    } else {
        LOG_DEBUG(log, "{}, ignoring folder {}/{}", identity, folder->id, folder->label);
        ignored_folders.put(folder);
        reply_to(**it);
    }
    ingored_folder_requests.erase(it);
}
#endif
