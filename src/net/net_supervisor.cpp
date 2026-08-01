// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "bouncer/messages.hpp"
#include "constants.h"
#include "db_actor.h"
#include "model/diff/advance/advance.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/peer/update_folder.h"
#include "net/names.h"
#include "net/net_supervisor.h"
#include "net/local_keeper.h"
#include "net/services_supervisor.h"
#include "net/scheduler.h"
#include "utils/format.hpp"
#include "utils/path_view.hpp"
#include "presentation/folder_entity.h"
#include "presentation/folder_entity.h"
#include "proto/proto-helpers-bep.h"
#include "proto/proto-helpers-db.h"

#include <ctime>

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t interrupt = 0;
} // namespace resource
} // namespace

net_supervisor_t::net_supervisor_t(net_supervisor_t::config_t &cfg)
    : parent_t(this, resource::interrupt, cfg), sequencer{cfg.sequencer}, app_config{cfg.app_config},
      independent_threads{cfg.independent_threads}, thread_counter{independent_threads},
      local_counter{cfg.local_counter} {
    bouncer = cfg.bouncer_address;
    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);

    auto log = utils::get_logger(names::coordinator);
    auto cert_file = app_config.cert_file.get_view(allocator);
    auto key_file = app_config.key_file.get_view(allocator);
    auto result = utils::load_pair(cert_file, key_file);
    if (!result) {
        LOG_CRITICAL(log, "cannot load certificate/key pair: {}", result.error());
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

    auto &sim_writes = app_config.bep_config.blocks_simultaneous_write;
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
    });
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) {
            p.subscribe_actor(&net_supervisor_t::on_model_update);
            p.subscribe_actor(&net_supervisor_t::on_model_interrupt);
            p.subscribe_actor(&net_supervisor_t::on_model_subscribe);
            p.subscribe_actor(&net_supervisor_t::on_model_unsubscribe);
            p.subscribe_actor(&net_supervisor_t::on_load_cluster_success);
            p.subscribe_actor(&net_supervisor_t::on_load_cluster_fail);
            p.subscribe_actor(&net_supervisor_t::on_model_request);
            p.subscribe_actor(&net_supervisor_t::on_thread_up);
            p.subscribe_actor(&net_supervisor_t::on_thread_ready);
            p.subscribe_actor(&net_supervisor_t::on_ready);
            p.subscribe_actor(&net_supervisor_t::on_local_up);
            p.subscribe_actor(&net_supervisor_t::on_start_services);
            p.subscribe_actor(&net_supervisor_t::on_stop_services);
            p.subscribe_actor(&net_supervisor_t::on_restart_services);
        },
        r::plugin::config_phase_t::PREINIT);
}

void net_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    parent_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    LOG_TRACE(log, "on_child_shutdown, '{}' due to {} ", actor->get_identity(), reason);
    if (actor->get_address() == services_addr) {
        services_addr.reset();
    }
}

void net_supervisor_t::shutdown_start() noexcept {
    parent_t::shutdown_start();
    if (shutdown_flag) {
        *const_cast<std::atomic_bool *>(shutdown_flag) = true;
    }
}

void net_supervisor_t::shutdown_finish() noexcept {
    db_addr.reset();
    parent_t::shutdown_finish();
}

void net_supervisor_t::launch_early() noexcept {
    ++local_counter;
    thread_counter = independent_threads;
    auto timeout = shutdown_timeout * 9 / 10;

    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto config_path = app_config.config_path.get_view(allocator);
    auto db_path = config_path / utils::make_native_view("mdbx-db", allocator);

    db_addr = create_actor<db_actor_t>()
                  .timeout(timeout)
                  .bouncer_address(bouncer)
                  .db_dir(db_path.detach())
                  .db_config(app_config.db_config)
                  .cluster(cluster)
                  .max_files_per_diff(constants::diffs_batch)
                  .escalate_failure()
                  .finish()
                  ->get_address();
    ++local_counter;

    create_actor<local_keeper_t>()
        .concurrent_hashes(app_config.hasher_threads)
        .watcher_impl(syncspirit_watcher_impl)
        .sequencer(sequencer)
        .escalate_failure()
        .timeout(timeout)
        .finish();
    ++local_counter;

    create_actor<scheduler_t>().timeout(timeout).escalate_failure().finish();
    ++local_counter;

    for (auto &l : launchers) {
        l(cluster);
    }
}

void net_supervisor_t::seed_model() noexcept {
    thread_counter = independent_threads;
    route<model::payload::model_update_t>(address, db_addr, std::move(load_diff), nullptr);
}

void net_supervisor_t::on_load_cluster_fail(message::load_cluster_fail_t &message) noexcept {
    auto &ee = message.payload.ee;
    LOG_ERROR(log, "cannot load cluster : {}", ee);
    return do_shutdown(ee);
}

void net_supervisor_t::on_load_cluster_success(message::load_cluster_success_t &message) noexcept {
    LOG_TRACE(log, "on_load_cluster_success");

    send<model::payload::db_loaded_t>(address);
    load_diff = std::move(message.payload.diff);
    try_seed_model();
}

void net_supervisor_t::try_seed_model() noexcept {
    if (thread_counter == 1) { // -1 as no need to seed model to self
        seed_model();
    }
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
    try_seed_model();
}

void net_supervisor_t::on_local_up(model::message::local_up_t &) noexcept {
    --local_counter;
    LOG_DEBUG(log, "on_local_up, left = {}", local_counter);
    if (local_counter == 0) {
        send<model::payload::local_ready_t>(coordinator);
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
        auto message = r::make_message<payload::ready_t>(address);
        send<bouncer::payload::package_t>(bouncer, std::move(message));
    }
}

void net_supervisor_t::on_ready(message::ready_t &) noexcept {
    LOG_DEBUG(log, "on_ready, counter = {}", local_counter);
    spawn_services();
    send<model::payload::local_up_t>(coordinator);
}

void net_supervisor_t::spawn_services() noexcept {
    LOG_TRACE(log, "spawnign services");
    assert(!services_addr);
    auto factory = [this](r::supervisor_t &, const r::address_ptr_t &spawner) -> r::actor_ptr_t {
        auto timeout = shutdown_timeout * 9 / 10;
        auto actor = create_actor<services_supervisor_t>()
                         .timeout(timeout)
                         .cluster(cluster)
                         .app_config(app_config)
                         .strand(strand)
                         .ssl_pair(&ssl_pair)
                         .sequencer(sequencer)
                         .spawner_address(spawner)
                         .auto_restart(auto_restart_services)
                         .finish();
        services_addr = actor->get_address();
        return actor;
    };
    spawn(factory).restart_period(pt::seconds{5}).restart_policy(r::restart_policy_t::ask_actor).spawn();
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
    send<syncspirit::model::payload::thread_up_t>(address);
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
        if (folder) {
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

void net_supervisor_t::on_stop_services(message::stop_services_t &) noexcept {
    auto_restart_services = false;
    LOG_TRACE(log, "on_stop_services");
    auto ee = make_error(make_error_code(r::shutdown_code_t::normal));
    send<r::payload::shutdown_trigger_t>(address, services_addr, std::move(ee));
}

void net_supervisor_t::on_start_services(message::start_services_t &) noexcept {
    LOG_TRACE(log, "on_start_services");
    if (services_addr) {
        LOG_WARN(log, "services are already running");
        return;
    }
    auto_restart_services = true;
    spawn_services();
}

void net_supervisor_t::on_restart_services(message::restart_services_t &) noexcept {
    LOG_TRACE(log, "on_restart_services");
    auto_restart_services = true;
    if (!services_addr) {
        spawn_services();
        return;
    }
    auto ee = make_error(make_error_code(r::shutdown_code_t::normal));
    send<r::payload::shutdown_trigger_t>(address, services_addr, std::move(ee));
}
