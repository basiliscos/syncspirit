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
        p.subscribe_actor(&net_supervisor_t::on_model_update);
        p.subscribe_actor(&net_supervisor_t::on_block_update);
        p.subscribe_actor(&net_supervisor_t::on_contact_update);
        p.subscribe_actor(&net_supervisor_t::on_load_cluster);
        p.subscribe_actor(&net_supervisor_t::on_model_request);
        launch_early();
    });
}

void net_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    parent_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    LOG_TRACE(log, "{}, on_child_shutdown, '{}' due to {} ", identity, actor->get_identity(), reason->message());
    if (actor->get_identity() == "peer_supervisor") {
        auto aaa = static_cast<peer_supervisor_t*>(actor);
        //LOG_ERROR(log, "zzz, qs = {}, iq = {}",   aaa->queue.size(), aaa->inbound_queue.empty());
    }
    auto &child_addr = actor->get_address();
    if (ssdp_addr && child_addr == ssdp_addr) {
        return;
    }
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

void net_supervisor_t::shutdown_finish() noexcept {
    db_addr.reset();
    local_discovery_addr.reset();
    ssdp_addr.reset();
    cluster_sup.reset();
    peers_sup.reset();
    ra::supervisor_asio_t::shutdown_finish();
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

void net_supervisor_t::on_contact_update(message::contact_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_contact_update", identity);
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
        auto& folders = cluster->get_folders();
        size_t files = 0;
        for(const auto& it: folders) {
            auto& folder_info = it.item;
            if (!folder_info) {
                continue;
            }
            auto fi = folder_info->get_folder_infos().by_device(cluster->get_device());
            files += fi->get_file_infos().size();
        }
        LOG_DEBUG(log,
                  "{}, load cluster, devices = {}, folders = {}, local files = {}, blocks = {}, ignored devices = {}, ignored folders = {}",
                  identity, devices.size(), folders.size(), files, cluster->get_blocks().size(),
                  ignored_devices.size(), ignored_folders.size());

        cluster_sup = create_actor<cluster_supervisor_t>()
                           .timeout(shutdown_timeout * 9 / 10)
                           .strand(strand)
                           .cluster(cluster)
                           .bep_config(app_config.bep_config)
                           .hasher_threads(app_config.hasher_threads)
                           .finish();

        launch_ssdp();
        launch_net();
    }
    return outcome::success();
}


void net_supervisor_t::launch_net() noexcept {
    auto timeout = shutdown_timeout * 9 / 10;
    LOG_INFO(log, "{}, launching network services", identity);

    create_actor<acceptor_actor_t>().cluster(cluster).timeout(timeout).finish();
    peers_sup = create_actor<peer_supervisor_t>()
                     .cluster(cluster)
                     .ssl_pair(&ssl_pair)
                     .device_name(app_config.device_name)
                     .strand(strand)
                     .timeout(timeout)
                     .bep_config(app_config.bep_config)
                     .finish();

    auto &local_cfg = app_config.local_announce_config;
    if (local_cfg.enabled) {
        local_discovery_addr = create_actor<local_discovery_actor_t>()
                                   .port(local_cfg.port)
                                   .frequency(local_cfg.frequency)
                                   .cluster(cluster)
                                   .timeout(timeout)
                                   .finish()
                                   ->get_address();
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
        auto io_timeout = shutdown_timeout * 8 / 10;
        create_actor<http_actor_t>()
            .timeout(timeout)
            .request_timeout(io_timeout)
            .resolve_timeout(io_timeout)
            .registry_name(names::http11_gda)
            .keep_alive(true)
            .finish();

        auto port = app_config.upnp_config.external_port;
        create_actor<global_discovery_actor_t>()
            .timeout(timeout)
            .cluster(cluster)
            .ssl_pair(&ssl_pair)
            .announce_url(gcfg.announce_url)
            .device_id(std::move(global_device_id.value()))
            .rx_buff_size(gcfg.rx_buff_size)
            .io_timeout(gcfg.timeout)
            .finish();
    }

    auto dcfg = app_config.dialer_config;
    if (dcfg.enabled) {
        create_actor<dialer_actor_t>()
            .timeout(timeout)
            .dialer_config(dcfg)
            .cluster(cluster)
            .finish();
    }
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

void net_supervisor_t::launch_ssdp() noexcept {
    auto &cfg = app_config.upnp_config;
    if (cfg.enabled && ssdp_attempts < cfg.discovery_attempts) {
        auto timeout = shutdown_timeout / 2;
        ssdp_addr = create_actor<ssdp_actor_t>().timeout(timeout).upnp_config(cfg).cluster(cluster).finish()->get_address();
        ++ssdp_attempts;
        LOG_TRACE(log, "{}, launching ssdp, attempt #{}", identity, ssdp_attempts);
    }
}
