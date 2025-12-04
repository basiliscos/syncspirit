// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "governor_actor.h"
#include "net/names.h"
#include "fs/messages.h"
#include "utils/format.hpp"
#include "utils/error_code.h"
#include "model/diff/advance/remote_copy.h"
#include "model/diff/local/io_failure.h"
#include "model/diff/modify/block_ack.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/peer/update_folder.h"

using namespace syncspirit::daemon;

governor_actor_t::governor_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, commands{std::move(cfg.commands)}, sequencer{std::move(cfg.sequencer)},
      cluster{std::move(cfg.cluster)} {}

void governor_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("deamon.governor", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ec) {
            if (!ec && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&governor_actor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&governor_actor_t::on_command);
            }
        });
    });
}

void governor_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    process();
    r::actor_base_t::on_start();
}

void governor_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    r::actor_base_t::shutdown_start();
}

void governor_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    auto custom = message.payload.custom;
    LOG_TRACE(log, "on_model_update, this = {}, payload = {}", (void *)this, custom);
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void governor_actor_t::send_command(model::diff::cluster_diff_ptr_t diff, command_t &source) noexcept {
    route<model::payload::model_update_t>(coordinator, address, std::move(diff), &source);
}

void governor_actor_t::on_command(model::message::model_update_t &message) noexcept {
    auto custom = message.payload.custom;
    LOG_TRACE(log, "on_command, this = {}, payload = {}", (void *)this, custom);
    for (auto it = commands.begin(); it != commands.end(); ++it) {
        if (it->get() == custom) {
            (*it)->finish();
            commands.erase(it);
            process();
            break;
        }
    }
}

void governor_actor_t::process() noexcept {
    LOG_DEBUG(log, "process");
NEXT:
    if (commands.empty()) {
        log->debug("no commands left for processing");
        return;
    }
    auto &cmd = commands.front();
    bool keep = cmd->execute(*this);
    if (!keep) {
        commands.pop_front();
        goto NEXT;
    }
}

void governor_actor_t::track_inactivity() noexcept {
    LOG_DEBUG(log, "will track inactivity for the next {} seconds", inactivity_seconds);
    assert(inactivity_seconds);
    auto timeout = r::pt::seconds(inactivity_seconds);
    auto now = clock_t::local_time();
    deadline = now + timeout;
    start_timer(timeout, *this, &governor_actor_t::on_inactivity_timer);
}

void governor_actor_t::on_inactivity_timer(r::request_id_t, bool cancelled) noexcept {
    LOG_DEBUG(log, "on_inactivity_timer");
    if (cancelled) {
        if (state == r::state_t::OPERATIONAL) {
            track_inactivity();
        }
        return;
    }

    auto now = clock_t::local_time();
    if (now < deadline) {
        return track_inactivity();
    }
    LOG_INFO(log, "inactivity timeout, exiting...");
    do_shutdown();
}

void governor_actor_t::refresh_deadline() noexcept {
    auto timeout = r::pt::seconds(inactivity_seconds);
    auto now = clock_t::local_time();
    deadline = now + timeout;
}

auto governor_actor_t::operator()(const model::diff::advance::remote_copy_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    refresh_deadline();
    return diff.visit_next(*this, custom);
}

auto governor_actor_t::operator()(const model::diff::local::io_failure_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    LOG_TRACE(log, "on_io_error, count = {}", diff.errors.size());
    for (auto &err : diff.errors) {
        LOG_WARN(log, "on_io_error (ignored) path: {}, problem: {}", err.path, err.ec.message());
    }
    return diff.visit_next(*this, custom);
}

auto governor_actor_t::operator()(const model::diff::peer::cluster_update_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    refresh_deadline();
    return diff.visit_next(*this, custom);
}

auto governor_actor_t::operator()(const model::diff::peer::update_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    refresh_deadline();
    return diff.visit_next(*this, custom);
}

auto governor_actor_t::operator()(const model::diff::modify::block_ack_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    refresh_deadline();
    return diff.visit_next(*this, custom);
}
