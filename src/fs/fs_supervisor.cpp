// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "fs_supervisor.h"
#include "model/diff/load/commit.h"
#include "model/diff/load/interrupt.h"
#include "net/names.h"
#include "scan_actor.h"
#include "scan_scheduler.h"
#include "file_actor.h"
#include "model/diff/load/load_cluster.h"
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

using apply_context_t = syncspirit::model::payload::model_interrupt_t;

namespace resource {
r::plugin::resource_id_t model = 0;
}
} // namespace

fs_supervisor_t::fs_supervisor_t(config_t &cfg)
    : parent_t(cfg), sequencer(cfg.sequencer), fs_config{cfg.fs_config}, hasher_threads{cfg.hasher_threads} {
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
                request<model::payload::model_request_t>(coordinator).send(init_timeout);
                resources->acquire(resource::model);
            }
        });
        p.discover_name(net::names::bouncer, bouncer, true).link(true);
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

void fs_supervisor_t::on_model_interrupt(model::message::model_interrupt_t &message) noexcept {
    LOG_TRACE(log, "on_model_interrupt");
    auto copy = message.payload;
    copy.diff = {};
    process(*message.payload.diff, &copy, {});
    while (!interrupted && delayed_updates.size()) {
        LOG_TRACE(log, "applying delayed model update");
        auto &msg = delayed_updates.front();
        auto &p = msg->payload;
        auto apply_ctx = apply_context_t{};
        process(*p.diff, &apply_ctx, p.custom);
        delayed_updates.pop_front();
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

void fs_supervisor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    if (interrupted) {
        delayed_updates.emplace_back(&message);
    } else {
        auto &p = message.payload;
        auto apply_ctx = apply_context_t{};
        process(*p.diff, &apply_ctx, p.custom);
    }
}

void fs_supervisor_t::process(model::diff::cluster_diff_t &diff, void *apply_context, const void *custom) noexcept {
    auto r = diff.apply(*cluster, *this, apply_context);
    if (!r) {
        LOG_ERROR(log, "error applying model diff: {}", r.assume_error().message());
        auto ee = make_error(r.assume_error());
        return do_shutdown(ee);
    }

    r = diff.visit(*this, nullptr);
    if (!r) {
        LOG_ERROR(log, "{}, error visiting model: {}", identity, r.assume_error().message());
        return do_shutdown(make_error(r.assume_error()));
    }

    auto apply_ctx = reinterpret_cast<apply_context_t *>(apply_context);
    interrupted = (bool)apply_ctx->diff;
    if (interrupted) {
        auto message = r::make_message<model::payload::model_interrupt_t>(address, std::move(*apply_ctx));
        send<hasher::payload::package_t>(bouncer, message);
    }
}

auto fs_supervisor_t::apply(const model::diff::load::interrupt_t &diff, model::cluster_t &cluster,
                            void *custom) noexcept -> outcome::result<void> {
    auto ctx = static_cast<apply_context_t *>(custom);
    ctx->diff = diff.sibling;
    return outcome::success();
}

auto fs_supervisor_t::apply(const model::diff::load::commit_t &diff, model::cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    log->debug("committing db load, begin");
    put(diff.commit_message);

    for (auto &it : cluster.get_folders()) {
        auto &folder = it.item;
        auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
        folder->set_augmentation(folder_entity);
    }
    send<syncspirit::model::payload::thread_ready_t>(coordinator);

    log->debug("committing db load, end");
    return outcome::success();
}

auto fs_supervisor_t::operator()(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto folder_id = db::get_id(diff.db);
    auto folder = cluster->get_folders().by_id(folder_id);
    if (!folder->get_augmentation()) {
        auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
        folder->set_augmentation(folder_entity);
    }
    return diff.visit_next(*this, custom);
}

auto fs_supervisor_t::operator()(const model::diff::modify::upsert_folder_info_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto r = diff.visit_next(*this, custom);
    auto &folder = *cluster->get_folders().by_id(diff.folder_id);
    auto &device = *cluster->get_devices().by_sha256(diff.device_id);
    auto folder_info = folder.is_shared_with(device);
    if (&device != cluster->get_device()) {
        auto augmentation = folder.get_augmentation().get();
        auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation);
        folder_entity->on_insert(*folder_info);
    }
    return r;
}

auto fs_supervisor_t::operator()(const model::diff::advance::advance_t &diff, void *custom) noexcept
    -> outcome::result<void> {
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
    return diff.visit_next(*this, custom);
}

auto fs_supervisor_t::operator()(const model::diff::peer::update_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
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
    return diff.visit_next(*this, custom);
}
