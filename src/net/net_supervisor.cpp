// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "bouncer/messages.hpp"
#include "cluster_supervisor.h"
#include "db_actor.h"
#include "local_discovery_actor.h"
#include "model/diff/advance/advance.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/peer/update_folder.h"
#include "net/acceptor_actor.h"
#include "net/dialer_actor.h"
#include "net/global_discovery_actor.h"
#include "net/http_actor.h"
#include "net/names.h"
#include "net/net_supervisor.h"
#include "net/peer_supervisor.h"
#include "net/relay_actor.h"
#include "net/resolver_actor.h"
#include "net/ssdp_actor.h"
#include "net/local_keeper.h"
#include "fs/scan_scheduler.h"
#include "presentation/folder_entity.h"
#include "presentation/folder_entity.h"
#include "proto/proto-helpers-bep.h"
#include "proto/proto-helpers-db.h"
#include "utils/io.h"

#include <boost/nowide/convert.hpp>
#include <filesystem>
#include <ctime>

namespace bfs = std::filesystem;
using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t interrupt = 0;
} // namespace resource
} // namespace

net_supervisor_t::net_supervisor_t(net_supervisor_t::config_t &cfg)
    : parent_t(this, resource::interrupt, cfg), sequencer{cfg.sequencer}, app_config{cfg.app_config},
      independent_threads{cfg.independent_threads}, thread_counter{independent_threads} {
    using boost::nowide::narrow;
    bouncer = cfg.bouncer_address;
    auto log = utils::get_logger(names::coordinator);
    auto cert_file = narrow(app_config.cert_file.wstring());
    auto key_file = narrow(app_config.key_file.wstring());
    auto result = utils::load_pair(cert_file.c_str(), key_file.c_str());
    if (!result) {
        LOG_CRITICAL(log, "cannot load certificate/key pair :: {}", result.error().message());
        throw result.error();
    }
    ssl_pair = std::move(result.value());
    auto device_id_opt = model::device_id_t::from_cert(ssl_pair.cert_data);
    if (!device_id_opt) {
        LOG_CRITICAL(log, "cannot create device_id from certificate");
        throw "cannot create device_id from certificate";
    }
    auto &device_id = device_id_opt.value();
    LOG_INFO(log, "{}, device name = {}, device id = {}, model threads = {}", names::coordinator,
             app_config.device_name, device_id.get_value(), independent_threads);

    auto cn = utils::get_common_name(ssl_pair.cert.get());
    if (!cn) {
        LOG_CRITICAL(log, "cannot get common name from certificate");
        throw "cannot get common name from certificate";
    }
    auto device_opt = model::device_t::create(device_id, app_config.device_name, cn.value());
    if (!device_opt) {
        LOG_CRITICAL(log, "cannot get common name from certificate");
        throw "cannot get common name from certificate";
    }

    if (!app_config.root_ca_file.empty()) {
        auto &file = app_config.root_ca_file;
        LOG_TRACE(log, "going to read root ca file fron '{}'", file.string());
        using in_t = utils::ifstream_t;
        auto path = bfs::path(file);
        auto ec = sys::error_code();
        auto size = bfs::file_size(path, ec);
        if (ec) {
            LOG_WARN(log, "cannot read root ca file size: {}", ec.message());
        } else {
            auto in = in_t(path, in_t::in | in_t::binary);
            auto data = utils::bytes_t(size);
            in.read(reinterpret_cast<char *>(data.data()), size);
            if (!in.fail()) {
                root_ca = std::move(data);
            } else {
                LOG_WARN(log, "cannot read root ca file '{}'", file.string());
            }
        }
    }

    auto &mru_size = app_config.fs_config.mru_size;
    auto &sim_writes = app_config.bep_config.blocks_simultaneous_write;
    if (app_config.fs_config.mru_size <= app_config.bep_config.blocks_simultaneous_write) {
        LOG_WARN(log, "config fs_config.mru_size ({}) <= bep_config.blocks_simultaneous_write ({})",
                 app_config.fs_config.mru_size, app_config.bep_config.blocks_simultaneous_write);
        sim_writes = mru_size - 2;
    }

    auto device = model::device_ptr_t();
    device = new model::local_device_t(device_id, app_config.device_name, cn.value());
    auto simultaneous_writes = app_config.bep_config.blocks_simultaneous_write;
    cluster = new model::cluster_t(device, static_cast<int32_t>(simultaneous_writes));
}

void net_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(names::coordinator, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        coordinator = address;
        p.register_name(names::coordinator, get_address());
        send<syncspirit::model::payload::thread_up_t>(address);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) {
            p.subscribe_actor(&net_supervisor_t::on_model_update);
            p.subscribe_actor(&net_supervisor_t::on_model_interrupt);
            p.subscribe_actor(&net_supervisor_t::on_load_cluster_success);
            p.subscribe_actor(&net_supervisor_t::on_load_cluster_fail);
            p.subscribe_actor(&net_supervisor_t::on_model_request);
            p.subscribe_actor(&net_supervisor_t::on_thread_up);
            p.subscribe_actor(&net_supervisor_t::on_thread_ready);
            p.subscribe_actor(&net_supervisor_t::on_app_ready);
        },
        r::plugin::config_phase_t::PREINIT);
}

void net_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    parent_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    LOG_TRACE(log, "on_child_shutdown, '{}' due to {} ", actor->get_identity(), reason->message());
}

void net_supervisor_t::shutdown_finish() noexcept {
    db_addr.reset();
    ra::supervisor_asio_t::shutdown_finish();
}

void net_supervisor_t::launch_early() noexcept {
    thread_counter = independent_threads;
    auto timeout = shutdown_timeout * 9 / 10;
    db_addr = create_actor<db_actor_t>()
                  .timeout(timeout)
                  .bouncer_address(bouncer)
                  .db_dir(app_config.config_path / "mdbx-db")
                  .db_config(app_config.db_config)
                  .cluster(cluster)
                  .escalate_failure()
                  .finish()
                  ->get_address();

    create_actor<local_keeper_t>()
        .concurrent_hashes(app_config.hasher_threads)
        .files_scan_iteration_limit(app_config.fs_config.files_scan_iteration_limit)
        .sequencer(sequencer)
        .escalate_failure()
        .timeout(timeout)
        .finish();

    create_actor<fs::scan_scheduler_t>().timeout(timeout).escalate_failure().finish();
}

void net_supervisor_t::seed_model() noexcept {
    thread_counter = independent_threads;
    route<model::payload::model_update_t>(address, db_addr, std::move(load_diff), nullptr);
}

void net_supervisor_t::on_load_cluster_fail(message::load_cluster_fail_t &message) noexcept {
    auto &ee = message.payload.ee;
    LOG_ERROR(log, "cannot load cluster : {}", ee->message());
    return do_shutdown(ee);
}

void net_supervisor_t::on_load_cluster_success(message::load_cluster_success_t &message) noexcept {
    LOG_TRACE(log, "on_load_cluster_success");

    send<model::payload::db_loaded_t>(address);
    load_diff = std::move(message.payload.diff);
}

void net_supervisor_t::on_model_request(model::message::model_request_t &message) noexcept {
    --thread_counter;
    LOG_TRACE(log, "on_cluster_seed, left = {}", thread_counter);
    auto my_device = cluster->get_device();
    auto device = model::device_ptr_t();
    device = new model::local_device_t(my_device->device_id(), app_config.device_name, "");
    auto simultaneous_writes = app_config.bep_config.blocks_simultaneous_write;
    auto cluster_copy = new model::cluster_t(device, static_cast<int32_t>(simultaneous_writes));
    reply_to(message, std::move(cluster_copy));
    assert(load_diff);
    if (thread_counter == 1) { // -1 as no need to seed model to self
        seed_model();
    }
}

void net_supervisor_t::on_thread_up(model::message::thread_up_t &) noexcept {
    --thread_counter;
    LOG_DEBUG(log, "on_thread_up, left = {}", thread_counter);
    if (thread_counter == 0) {
        launch_early();
    }
}

void net_supervisor_t::on_thread_ready(model::message::thread_ready_t &) noexcept {
    --thread_counter;
    LOG_DEBUG(log, "on_thread_ready, left = {}", thread_counter);
    if (thread_counter == 0 && state == r::state_t::OPERATIONAL) {
        // thread_ready_t messages are routed, give let routed messages be processed 1st
        auto message = r::make_message<model::payload::app_ready_t>(address);
        send<bouncer::payload::package_t>(bouncer, std::move(message));
    }
}

void net_supervisor_t::on_app_ready(model::message::app_ready_t &) noexcept {
    LOG_DEBUG(log, "on_app_ready");

    cluster_sup = create_actor<cluster_supervisor_t>()
                      .timeout(shutdown_timeout * 9 / 10)
                      .strand(strand)
                      .cluster(cluster)
                      .sequencer(sequencer)
                      .config(app_config)
                      .escalate_failure()
                      .finish();

    if (app_config.upnp_config.enabled) {
        auto factory = [this](r::supervisor_t &, const r::address_ptr_t &spawner) -> r::actor_ptr_t {
            auto timeout = shutdown_timeout * 8 / 10;
            return create_actor<ssdp_actor_t>()
                .timeout(timeout)
                .upnp_config(app_config.upnp_config)
                .cluster(cluster)
                .spawner_address(spawner)
                .finish();
        };
        spawn(factory).restart_period(pt::seconds{5}).restart_policy(r::restart_policy_t::fail_only).spawn();
    }

    if (app_config.local_announce_config.enabled) {
        auto factory = [this](r::supervisor_t &, const r::address_ptr_t &spawner) -> r::actor_ptr_t {
            auto timeout = shutdown_timeout * 9 / 10;
            auto &cfg = app_config.local_announce_config;
            return create_actor<local_discovery_actor_t>()
                .port(cfg.port)
                .frequency(cfg.frequency)
                .cluster(cluster)
                .timeout(timeout)
                .spawner_address(spawner)
                .finish();
        };
        spawn(factory).restart_period(pt::seconds{5}).restart_policy(r::restart_policy_t::fail_only).spawn();
    }

    if (app_config.global_announce_config.enabled) {
        auto timeout = shutdown_timeout * 9 / 10;
        auto io_timeout = shutdown_timeout * 8 / 10;
        create_actor<http_actor_t>()
            .timeout(timeout)
            .request_timeout(io_timeout)
            .resolve_timeout(io_timeout)
            .registry_name(names::http11_gda)
            .root_ca(root_ca)
            .keep_alive(true)
            .escalate_failure()
            .finish();

        auto factory = [this](r::supervisor_t &, const r::address_ptr_t &spawner) -> r::actor_ptr_t {
            auto &gcfg = app_config.global_announce_config;
            auto timeout = shutdown_timeout * 9 / 10;
            return create_actor<global_discovery_actor_t>()
                .timeout(timeout)
                .cluster(cluster)
                .ssl_pair(&ssl_pair)
                .announce_url(gcfg.announce_url)
                .lookup_url(gcfg.lookup_url)
                .rx_buff_size(gcfg.rx_buff_size)
                .io_timeout(gcfg.timeout)
                .debug(gcfg.debug)
                .spawner_address(spawner)
                .finish();
        };
        spawn(factory).restart_period(pt::seconds{5}).restart_policy(r::restart_policy_t::fail_only).spawn();
    }

    if (app_config.relay_config.enabled) {
        auto timeout = shutdown_timeout * 9 / 10;
        auto io_timeout = shutdown_timeout * 8 / 10;
        create_actor<http_actor_t>()
            .timeout(timeout)
            .request_timeout(io_timeout)
            .resolve_timeout(io_timeout)
            .registry_name(names::http11_relay)
            .root_ca(root_ca)
            .keep_alive(true)
            .escalate_failure()
            .finish();

        auto factory = [this](r::supervisor_t &, const r::address_ptr_t &spawner) -> r::actor_ptr_t {
            auto timeout = shutdown_timeout * 9 / 10;
            return create_actor<relay_actor_t>()
                .timeout(timeout)
                .relay_config(app_config.relay_config)
                .cluster(cluster)
                .root_ca(root_ca)
                .spawner_address(spawner)
                .finish();
        };
        spawn(factory).restart_period(pt::seconds{5}).restart_policy(r::restart_policy_t::fail_only).spawn();
    }

    auto timeout = shutdown_timeout * 9 / 10;
    create_actor<acceptor_actor_t>().cluster(cluster).timeout(timeout).escalate_failure().finish();
    peer_sup = create_actor<peer_supervisor_t>()
                   .cluster(cluster)
                   .ssl_pair(&ssl_pair)
                   .device_name(app_config.device_name)
                   .strand(strand)
                   .timeout(timeout)
                   .bep_config(app_config.bep_config)
                   .relay_config(app_config.relay_config)
                   .escalate_failure()
                   .finish();
    auto dcfg = app_config.dialer_config;
    if (dcfg.enabled) {
        create_actor<dialer_actor_t>().timeout(timeout).dialer_config(dcfg).cluster(cluster).finish();
    }
}

void net_supervisor_t::commit_loading() noexcept {
    if (!cluster->is_tainted()) {
        for (auto &it : cluster->get_folders()) {
            auto &folder = it.item;
            auto folder_entity = presentation::folder_entity_ptr_t(new presentation::folder_entity_t(folder));
            folder->set_augmentation(folder_entity);
        }

        auto &ignored_devices = cluster->get_ignored_devices();
        auto &ignored_folders = cluster->get_ignored_folders();
        auto &pending_folders = cluster->get_pending_folders();
        auto &pending_devices = cluster->get_pending_devices();
        auto &devices = cluster->get_devices();
        auto &folders = cluster->get_folders();
        size_t files = 0;
        for (const auto &it : folders) {
            auto &folder_info = it.item;
            if (!folder_info) {
                continue;
            }
            auto fi = folder_info->get_folder_infos().by_device(*cluster->get_device());
            files += fi->get_file_infos().size();
        }
        auto pending_folders_sz = std::distance(pending_folders.begin(), pending_folders.end());
        LOG_DEBUG(log,
                  "load cluster, devices = {}, folders = {}, local files = {}, blocks = {}, ignored devices = {}, "
                  "ignored folders = {}, pending folders = {}, pending devices = {}",
                  devices.size(), folders.size(), files, cluster->get_blocks().size(), ignored_devices.size(),
                  ignored_folders.size(), pending_folders_sz, pending_devices.size());
    }
}

void net_supervisor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
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
        .escalate_failure()
        .finish();
}

auto net_supervisor_t::apply(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto r = parent_t::apply(diff, custom);
    if (r) {
        auto folder_id = db::get_id(diff.db);
        auto folder = cluster->get_folders().by_id(folder_id);
        if (!folder->get_augmentation()) {
            auto folder_entity = presentation::folder_entity_ptr_t(new presentation::folder_entity_t(folder));
            folder->set_augmentation(folder_entity);
        }
    }
    return r;
}

auto net_supervisor_t::apply(const model::diff::modify::upsert_folder_info_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto r = parent_t::apply(diff, custom);
    if (r) {
        auto &folder = *cluster->get_folders().by_id(diff.folder_id);
        auto &device = *cluster->get_devices().by_sha256(diff.device_id);
        auto folder_info = folder.is_shared_with(device);
        if (&device != cluster->get_device()) {
            auto augmentation = folder.get_augmentation().get();
            auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation);
            folder_entity->on_insert(*folder_info);
        }
    }
    return r;
}

auto net_supervisor_t::apply(const model::diff::advance::advance_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto r = parent_t::apply(diff, custom);
    if (r) {
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        auto augmentation = folder->get_augmentation().get();
        auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation);
        if (folder_entity) {
            auto &folder_infos = folder->get_folder_infos();
            auto &local_fi = *folder_infos.by_device(*cluster->get_device());
            auto file_name = proto::get_name(diff.proto_local);
            auto local_file = local_fi.get_file_infos().by_name(file_name);
            if (local_file) {
                folder_entity->on_insert(*local_file, local_fi);
            }
        }
    }
    return r;
}

auto net_supervisor_t::apply(const model::diff::peer::update_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto r = parent_t::apply(diff, custom);
    if (r) {
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        auto folder_aug = folder->get_augmentation().get();
        auto folder_entity = static_cast<presentation::folder_entity_t *>(folder_aug);

        auto &devices_map = cluster->get_devices();
        auto peer = devices_map.by_sha256(diff.peer_id);
        auto &folder_info = *folder->get_folder_infos().by_device(*peer);
        auto &files_map = folder_info.get_file_infos();

        for (auto &file : diff.files) {
            auto file_name = proto::get_name(file);
            auto file_info = files_map.by_name(file_name);
            auto augmentation = file_info->get_augmentation().get();
            if (!augmentation) {
                folder_entity->on_insert(*file_info, folder_info);
            }
        }
    }
    return r;
}
