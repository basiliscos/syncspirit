// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "fs_supervisor.h"
#include "model/diff/load/commit.h"
#include "model/diff/load/interrupt.h"
#include "net/names.h"
#include "scan_actor.h"
#include "scan_scheduler.h"
#include "file_actor.h"
#include "model/diff/advance/advance.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/peer/update_folder.h"
#include "presentation/folder_entity.h"
#include "proto/proto-helpers-bep.h"
#include "proto/proto-helpers-db.h"

using namespace syncspirit::fs;
using namespace syncspirit::presentation;

namespace {

namespace resource {
r::plugin::resource_id_t model = 0;
}
} // namespace

fs_supervisor_t::fs_supervisor_t(config_t &cfg)
    : parent_t(this, cfg), sequencer(cfg.sequencer), fs_config{cfg.fs_config}, hasher_threads{cfg.hasher_threads} {
    rw_cache.reset(new file_cache_t(fs_config.mru_size));
}

void fs_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("fs.supervisor", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&fs_supervisor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&fs_supervisor_t::on_app_ready, coordinator);
                plugin->subscribe_actor(&fs_supervisor_t::on_db_loaded, coordinator);
                send<syncspirit::model::payload::thread_up_t>(coordinator);
                resources->acquire(resource::model);
            }
        });
        p.discover_name(net::names::bouncer, bouncer, true).link(false);
    });

    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) {
            p.subscribe_actor(&fs_supervisor_t::on_model_interrupt);
            p.subscribe_actor(&fs_supervisor_t::on_model_request);
            p.subscribe_actor(&fs_supervisor_t::on_model_response);
        },
        r::plugin::config_phase_t::PREINIT);
}

void fs_supervisor_t::launch() noexcept {
    LOG_DEBUG(log, "launching children actors");
    auto factory = [this](r::supervisor_t &, const r::address_ptr_t &spawner) -> r::actor_ptr_t {
        auto timeout = shutdown_timeout * 9 / 10;
        return create_actor<file_actor_t>()
            .cluster(cluster)
            .sequencer(sequencer)
            .rw_cache(rw_cache)
            .timeout(timeout)
            .spawner_address(spawner)
            .finish();
    };
    spawn(factory).restart_period(r::pt::seconds{1}).restart_policy(r::restart_policy_t::fail_only).spawn();

    auto timeout = shutdown_timeout * 9 / 10;
    scan_actor = create_actor<scan_actor_t>()
                     .fs_config(fs_config)
                     .rw_cache(rw_cache)
                     .cluster(cluster)
                     .sequencer(sequencer)
                     .requested_hashes_limit(hasher_threads * 2)
                     .timeout(timeout)
                     .finish();

    create_actor<scan_scheduler_t>().cluster(cluster).timeout(timeout).finish();

    for (auto &l : launchers) {
        l(cluster);
    }
}

void fs_supervisor_t::on_db_loaded(model::message::db_loaded_t &) noexcept {
    LOG_TRACE(log, "on_db_loaded");
    request<model::payload::model_request_t>(coordinator).send(init_timeout);
}

void fs_supervisor_t::on_model_request(model::message::model_request_t &req) noexcept {
    LOG_TRACE(log, "on_model_request");
    if (cluster) {
        LOG_TRACE(log, "already have cluster, share it");
        reply_to(req, cluster);
        return;
    }
    LOG_TRACE(log, "no cluster, delaying response");
    model_request = &req;
}

void fs_supervisor_t::on_model_response(model::message::model_response_t &res) noexcept {
    LOG_TRACE(log, "on_model_response");
    resources->release(resource::model);
    auto &ee = res.payload.ee;
    if (ee) {
        LOG_ERROR(log, "cannot get model: {}", ee->message());
        return do_shutdown(ee);
    }
    cluster = std::move(res.payload.res.cluster);
    if (model_request) {
        reply_to(*model_request, cluster);
    }
}

void fs_supervisor_t::on_app_ready(model::message::app_ready_t &) noexcept {
    LOG_TRACE(log, "on_app_ready");
    launch();
}

void fs_supervisor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
}

void fs_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    parent_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    LOG_TRACE(log, "on_child_shutdown, '{}' due to {} ", actor->get_identity(), reason->message());
}

void fs_supervisor_t::commit_loading() noexcept {
    for (auto &it : cluster->get_folders()) {
        auto &folder = it.item;
        auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
        folder->set_augmentation(folder_entity);
    }
}

auto fs_supervisor_t::apply(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto r = parent_t::apply(diff, custom);
    if (r) {
        auto folder_id = db::get_id(diff.db);
        auto folder = cluster->get_folders().by_id(folder_id);
        if (!folder->get_augmentation()) {
            auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
            folder->set_augmentation(folder_entity);
        }
    }
    return r;
}

auto fs_supervisor_t::apply(const model::diff::modify::upsert_folder_info_t &diff, void *custom) noexcept
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

auto fs_supervisor_t::apply(const model::diff::advance::advance_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto r = parent_t::apply(diff, custom);
    if (r) {
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        auto augmentation = folder->get_augmentation().get();
        auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation);
        if (folder_entity) {
            auto &folder_infos = folder->get_folder_infos();
            auto local_fi = folder_infos.by_device(*cluster->get_device());
            auto file_name = proto::get_name(diff.proto_local);
            auto local_file = local_fi->get_file_infos().by_name(file_name);
            if (local_file) {
                folder_entity->on_insert(*local_file);
            }
        }
    }
    return r;
}

auto fs_supervisor_t::apply(const model::diff::peer::update_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto r = parent_t::apply(diff, custom);
    if (r) {
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        auto folder_aug = folder->get_augmentation().get();
        auto folder_entity = static_cast<presentation::folder_entity_t *>(folder_aug);

        auto &devices_map = cluster->get_devices();
        auto peer = devices_map.by_sha256(diff.peer_id);
        auto &files_map = folder->get_folder_infos().by_device(*peer)->get_file_infos();

        for (auto &file : diff.files) {
            auto file_name = proto::get_name(file);
            auto file_info = files_map.by_name(file_name);
            auto augmentation = file_info->get_augmentation().get();
            if (!augmentation) {
                folder_entity->on_insert(*file_info);
            }
        }
    }
    return r;
}
