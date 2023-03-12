// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "fs_supervisor.h"
#include "net/names.h"
#include "hasher/hasher_proxy_actor.h"
#include "scan_actor.h"
#include "file_actor.h"

using namespace syncspirit::fs;

namespace {
namespace resource {
r::plugin::resource_id_t model = 0;
}
} // namespace

fs_supervisor_t::fs_supervisor_t(config_t &cfg)
    : parent_t(cfg), fs_config{cfg.fs_config}, hasher_threads{cfg.hasher_threads} {
    log = utils::get_logger("fs.supervisor");
}

void fs_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("fs::supervisor", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&fs_supervisor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&fs_supervisor_t::on_block_update, coordinator);
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
    auto &timeout = shutdown_timeout;
    create_actor<file_actor_t>().cluster(cluster).mru_size(fs_config.mru_size).timeout(timeout).finish();
    auto hasher_addr = create_actor<hasher::hasher_proxy_actor_t>()
                           .hasher_threads(hasher_threads)
                           .name("fs::hasher_proxy")
                           .timeout(timeout)
                           .finish()
                           ->get_address();
    scan_actor = create_actor<scan_actor_t>()
                     .fs_config(fs_config)
                     .cluster(cluster)
                     .hasher_proxy(hasher_addr)
                     .requested_hashes_limit(hasher_threads * 2)
                     .timeout(timeout)
                     .finish();
}

void fs_supervisor_t::on_model_request(model::message::model_request_t &req) noexcept {
    LOG_TRACE(log, "{}, on_model_request", identity);
    if (cluster) {
        LOG_TRACE(log, "{}, already have cluster, share it", identity);
        reply_to(req, cluster);
        return;
    }
    LOG_TRACE(log, "{}, no cluster, delaying response", identity);
    model_request = &req;
}

void fs_supervisor_t::on_model_response(model::message::model_response_t &res) noexcept {
    LOG_TRACE(log, "{}, on_model_response", identity);
    resources->release(resource::model);
    auto ee = res.payload.ee;
    if (ee) {
        LOG_ERROR(log, "{}, cannot get model: {}", identity, ee->message());
        return do_shutdown(ee);
    }
    cluster = std::move(res.payload.res.cluster);
    if (model_request) {
        reply_to(*model_request, cluster);
    }
    launch();
}

void fs_supervisor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}

void fs_supervisor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto &diff = *message.payload.diff;
    auto r = diff.apply(*cluster);
    if (!r) {
        auto ee = make_error(r.assume_error());
        return do_shutdown(ee);
    }
    send<model::payload::forwarded_model_update_t>(address, &message);
}

void fs_supervisor_t::on_block_update(model::message::block_update_t &message) noexcept {
    auto &diff = *message.payload.diff;
    LOG_TRACE(log, "{}, on_block_update for {}", identity, diff.file_name);
    auto r = diff.apply(*cluster);
    if (!r) {
        auto ee = make_error(r.assume_error());
        return do_shutdown(ee);
    }
    send<model::payload::forwarded_block_update_t>(address, &message);
}
