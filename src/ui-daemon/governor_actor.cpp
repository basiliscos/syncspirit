// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "governor_actor.h"
#include "net/names.h"
#include "utils/format.hpp"
#include "utils/error_code.h"

using namespace syncspirit::daemon;

governor_actor_t::governor_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, commands{std::move(cfg.commands)}, cluster{std::move(cfg.cluster)} {
    log = utils::get_logger("daemon.governor_actor");

    add_callback(this, [&]() {
        process();
        return false;
    });
}

void governor_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("governor", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ec) {
            if (!ec && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&governor_actor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&governor_actor_t::on_block_update, coordinator);
                plugin->subscribe_actor(&governor_actor_t::on_io_error, coordinator);
                plugin->subscribe_actor(&governor_actor_t::on_scan_completed, coordinator);
            }
        });
        p.discover_name(net::names::fs_scanner, fs_scanner, true).link(false);
    });
}

void governor_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    rescan_folders();
    process();
    r::actor_base_t::on_start();
}

void governor_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "{}, shutdown_start", identity);
    r::actor_base_t::shutdown_start();
}

void governor_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    auto custom = message.payload.custom;
    LOG_TRACE(log, "{}, on_model_update, this = {}, payload = {}", identity, (void *)this, custom);
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }

    auto it = callbacks_map.find(custom);
    if (it != callbacks_map.end()) {
        auto remove = it->second();
        if (remove) {
            callbacks_map.erase(it);
        }
    }
}

void governor_actor_t::on_block_update(model::message::block_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_block_update", identity);
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void governor_actor_t::on_io_error(model::message::io_error_t &reply) noexcept {
    auto &errs = reply.payload.errors;
    LOG_TRACE(log, "{}, on_io_error, count = {}", identity, errs.size());
    for (auto &err : errs) {
        LOG_WARN(log, "{}, on_io_error (ignored) path: {}, problem: {}", identity, err.path, err.ec.message());
    }
}

void governor_actor_t::on_scan_completed(fs::message::scan_completed_t &message) noexcept {
    auto &folder_id = message.payload.folder_id;
    auto folder = scaning_folders.by_id(folder_id);
    LOG_TRACE(log, "{}, on_scan_completed, folder = {}({})", identity, folder->get_label(), folder->get_id());
    scaning_folders.remove(folder);
}

void governor_actor_t::process() noexcept {
    LOG_DEBUG(log, "{}, process", identity);
NEXT:
    if (commands.empty()) {
        log->debug("{}, no commands left for processing", identity);
        return;
    }
    auto &cmd = commands.front();
    bool ok = cmd->execute(*this);
    commands.pop_front();
    if (!ok) {
        goto NEXT;
    }
}

void governor_actor_t::track_inactivity() noexcept {
    LOG_DEBUG(log, "{}, will track inactivity for the next {} seconds", identity, inactivity_seconds);
    assert(inactivity_seconds);
    auto timeout = r::pt::seconds(inactivity_seconds);
    auto now = clock_t::local_time();
    deadline = now + timeout;
    start_timer(timeout, *this, &governor_actor_t::on_inacitvity_timer);
}

void governor_actor_t::on_inacitvity_timer(r::request_id_t, bool cancelled) noexcept {
    LOG_DEBUG(log, "{}, on_inacitvity_timer", identity);
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
    LOG_INFO(log, "inactivity timeout, exiting...", identity);
    do_shutdown();
}

void governor_actor_t::on_rescan_timer(r::request_id_t, bool cancelled) noexcept {
    if (!cancelled) {
        rescan_folders();
        schedule_rescan_dirs();
    }
}

void governor_actor_t::refresh_deadline() noexcept {
    auto timeout = r::pt::seconds(inactivity_seconds);
    auto now = clock_t::local_time();
    deadline = now + timeout;
}

void governor_actor_t::schedule_rescan_dirs(const r::pt::time_duration &interval) noexcept {
    dirs_rescan_interval = interval;
    schedule_rescan_dirs();
}

void governor_actor_t::schedule_rescan_dirs() noexcept {
    LOG_INFO(log, "{}, scheduling dirs rescan", identity);
    start_timer(dirs_rescan_interval, *this, &governor_actor_t::on_rescan_timer);
}

void governor_actor_t::rescan_folders() {
    if (scaning_folders.size() == 0) {
        auto& folders = cluster->get_folders();
        LOG_INFO(log, "{}, issuing folders ({}) rescan", identity, folders.size());
        for (auto it : folders) {
            auto &folder = it.item;
            send<fs::payload::scan_folder_t>(fs_scanner, std::string(folder->get_id()));
            scaning_folders.put(folder);
        }
    }
}

void governor_actor_t::rescan_folder(std::string_view folder_id) noexcept {
    auto folder = cluster->get_folders().by_id(folder_id);
    LOG_INFO(log, "{}, forcing folder '{}' rescan", identity, folder->get_label());
    send<fs::payload::scan_folder_t>(fs_scanner, std::string(folder->get_id()));
    scaning_folders.put(folder);
}

void governor_actor_t::add_callback(const void *pointer, command_callback_t &&callback) noexcept {
    callbacks_map.emplace(pointer, callback);
}

auto governor_actor_t::operator()(const model::diff::modify::clone_file_t &, void *) noexcept -> outcome::result<void> {
    refresh_deadline();
    return outcome::success();
}

auto governor_actor_t::operator()(const model::diff::peer::cluster_update_t &, void *) noexcept
    -> outcome::result<void> {
    refresh_deadline();
    return outcome::success();
}

auto governor_actor_t::operator()(const model::diff::peer::update_folder_t &, void *) noexcept
    -> outcome::result<void> {
    refresh_deadline();
    return outcome::success();
}

auto governor_actor_t::operator()(const model::diff::modify::append_block_t &, void *) noexcept
    -> outcome::result<void> {
    refresh_deadline();
    return outcome::success();
}

auto governor_actor_t::operator()(const model::diff::modify::clone_block_t &, void *) noexcept
    -> outcome::result<void> {
    refresh_deadline();
    return outcome::success();
}
