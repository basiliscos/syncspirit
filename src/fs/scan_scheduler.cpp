// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "scan_scheduler.h"
#include "net/names.h"
#include "messages.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/local/scan_finish.h"

using namespace syncspirit::fs;

scan_scheduler_t::scan_scheduler_t(config_t &cfg) : r::actor_base_t(cfg), cluster{std::move(cfg.cluster)} {}

void scan_scheduler_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(net::names::fs_scheduler, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(net::names::fs_actor, address);
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&scan_scheduler_t::on_model_update, coordinator);
            }
        });
        p.discover_name(net::names::fs_scanner, fs_scanner, true).link(true);
    });
}

void scan_scheduler_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
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
    scan_next_or_schedule();
    return diff.visit_next(*this, custom);
}

auto scan_scheduler_t::operator()(const model::diff::local::scan_finish_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    scan_next_or_schedule();
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
            LOG_TRACE(log, "cancellling previous schedule");
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
    auto folder = model::folder_ptr_t();
    auto deadline = r::pt::ptime{};
    auto now = r::pt::second_clock::local_time();
    for (auto it : cluster->get_folders()) {
        auto interval = it.item->get_rescan_interval();
        if (!interval || it.item->is_scanning()) {
            continue;
        }
        auto interval_s = r::pt::seconds{interval};
        auto prev_scan = it.item->get_scan_start();
        auto it_deadline = prev_scan.is_not_a_date_time() ? now : prev_scan + interval_s;
        auto select_it = !folder || it_deadline < deadline ||
                         ((it_deadline == deadline) && (folder->get_rescan_interval() < interval));
        if (select_it) {
            folder = it.item;
            deadline = it_deadline;
        }
    }

    if (folder && deadline <= now) {
        send<fs::payload::scan_folder_t>(fs_scanner, std::string(folder->get_id()));
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
            LOG_DEBUG(log, "sending folder {}({}) scan request", folder->get_label(), folder->get_id());
            send<fs::payload::scan_folder_t>(fs_scanner, std::move(folder_id));
        }
    }
}
