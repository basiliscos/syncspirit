// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "scan_scheduler.h"
#include "net/names.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/local/scan_request.h"
#include "model/diff/local/scan_start.h"
#include "model/diff/local/synchronization_finish.h"

using namespace syncspirit::fs;

scan_scheduler_t::scan_scheduler_t(config_t &cfg)
    : r::actor_base_t(cfg), cluster{std::move(cfg.cluster)}, scan_in_progress{false} {}

void scan_scheduler_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(net::names::fs_scheduler, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(net::names::fs_scheduler, address);
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&scan_scheduler_t::on_model_update, coordinator);
                plugin->subscribe_actor(&scan_scheduler_t::on_ui_ready, coordinator);
            }
        });
        p.discover_name(net::names::fs_scanner, fs_scanner, true).link();
    });
}

void scan_scheduler_t::on_ui_ready(model::message::ui_ready_t &) noexcept {
    LOG_TRACE(log, "on_ui_ready");
    scan_next();
}

void scan_scheduler_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto scan_scheduler_t::operator()(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (!scan_in_progress) {
        scan_next_or_schedule();
    }
    return diff.visit_next(*this, custom);
}

auto scan_scheduler_t::operator()(const model::diff::local::scan_request_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    scan_queue.emplace_back(diff.folder_id);
    if (!scan_in_progress) {
        scan_next_or_schedule();
    }
    return diff.visit_next(*this, custom);
}

auto scan_scheduler_t::operator()(const model::diff::local::scan_finish_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    scan_in_progress = false;
    scan_next_or_schedule();
    return diff.visit_next(*this, custom);
}

auto scan_scheduler_t::operator()(const model::diff::local::synchronization_finish_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (!scan_in_progress) {
        scan_next_or_schedule();
    }
    return diff.visit_next(*this, custom);
}

void scan_scheduler_t::scan_next_or_schedule() noexcept {
    auto next = scan_next();
    if (!next) {
        return;
    }
    bool do_start_timer = false;
    if (timer_id) {
        if (schedule_option->at > next.value().at) {
            LOG_TRACE(log, "cancelling previous schedule");
            cancel_timer(*timer_id);
            do_start_timer = true;
        }
    } else {
        do_start_timer = true;
    }
    if (do_start_timer) {
        LOG_DEBUG(log, "scheduling folders after {}s", next->interval.total_seconds());
        timer_id = start_timer(next->interval, *this, &scan_scheduler_t::on_timer);
        schedule_option = next;
    }
}

auto scan_scheduler_t::scan_next() noexcept -> schedule_option_t {
    while (!scan_queue.empty()) {
        auto folder_id = scan_queue.front();
        scan_queue.pop_front();
        auto folder = cluster->get_folders().by_id(folder_id);
        if (!folder || folder->is_synchronizing() || folder->is_suspended()) {
            continue;
        }
        for (auto it = scan_queue.begin(); it != scan_queue.end();) {
            if (*it == folder_id) {
                it = scan_queue.erase(it);
            } else {
                ++it;
            }
        }
        initiate_scan(folder_id);
        return {};
    }

    auto folder = model::folder_ptr_t();
    auto deadline = r::pt::ptime{};
    auto now = r::pt::second_clock::local_time();
    for (auto it : cluster->get_folders()) {
        auto &f = it.item;
        auto interval = f->get_rescan_interval();
        if (!interval || f->is_scanning() || f->is_synchronizing() || f->is_suspended()) {
            continue;
        }
        auto interval_s = r::pt::seconds{interval};
        auto prev_scan = f->get_scan_finish();
        auto it_deadline = prev_scan.is_not_a_date_time() ? now : prev_scan + interval_s;
        auto select_it = !folder || it_deadline < deadline ||
                         ((it_deadline == deadline) && (folder->get_rescan_interval() > interval));
        if (select_it) {
            folder = f;
            deadline = it_deadline;
        }
    }

    if (folder && deadline <= now) {
        initiate_scan(folder->get_id());
        return {};
    }
    if (folder) {
        auto folder_id = std::string(folder->get_id());
        return next_schedule_t{deadline - now, deadline, folder_id};
    }
    return {};
}

void scan_scheduler_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    timer_id = {};
    if (!cancelled) {
        auto &folder_id = schedule_option->folder_id;
        if (auto folder = cluster->get_folders().by_id(folder_id); folder) {
            initiate_scan(folder->get_id());
        }
    }
}

void scan_scheduler_t::initiate_scan(std::string_view folder_id) noexcept {
    LOG_DEBUG(log, "iniating folder '{}' scan", folder_id);
    auto diff = model::diff::cluster_diff_ptr_t{};
    auto now = r::pt::microsec_clock::local_time();
    diff = new model::diff::local::scan_start_t(folder_id, now);
    send<model::payload::model_update_t>(coordinator, std::move(diff));
    scan_in_progress = true;
}
