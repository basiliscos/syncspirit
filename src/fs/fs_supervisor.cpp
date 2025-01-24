// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "fs_supervisor.h"
#include "net/names.h"
#include "scan_actor.h"
#include "scan_scheduler.h"
#include "file_actor.h"

using namespace syncspirit::fs;

namespace {
namespace resource {
r::plugin::resource_id_t model = 0;
}
} // namespace

fs_supervisor_t::fs_supervisor_t(config_t &cfg)
    : parent_t(cfg), sequencer(cfg.sequencer), fs_config{cfg.fs_config}, hasher_threads{cfg.hasher_threads} {}

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
                request<model::payload::model_request_t>(coordinator).send(init_timeout);
                resources->acquire(resource::model);
            }
        });
    });

    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) {
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
            .mru_size(fs_config.mru_size)
            .timeout(timeout)
            .spawner_address(spawner)
            .finish();
    };
    spawn(factory).restart_period(r::pt::seconds{1}).restart_policy(r::restart_policy_t::fail_only).spawn();

    auto timeout = shutdown_timeout * 9 / 10;
    scan_actor = create_actor<scan_actor_t>()
                     .fs_config(fs_config)
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
    launch();
}

void fs_supervisor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
}

void fs_supervisor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *message.payload.diff;
    auto r = diff.apply(*cluster, *this);
    if (!r) {
        LOG_ERROR(log, "error applying model diff: {}", r.assume_error().message());
        auto ee = make_error(r.assume_error());
        return do_shutdown(ee);
    }
}
