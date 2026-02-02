// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "scheduler.h"
#include "net/names.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/local/scan_request.h"
#include "model/diff/local/scan_start.h"
#include "model/diff/local/synchronization_finish.h"

using namespace syncspirit::net;

enum class subpath_comparison_t { equal, includes, is_included, no_intersection };

static inline subpath_comparison_t compare(std::string_view a, std::string_view b) noexcept {
    using C = subpath_comparison_t;
    if (a.size() < b.size()) {
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                return C::no_intersection;
            }
        }
        return C::includes;
    } else if (a.size() > b.size()) {
        for (size_t i = 0; i < b.size(); ++i) {
            if (a[i] != b[i]) {
                return C::no_intersection;
            }
        }
        return C::is_included;
    } else {
        return a == b ? C::equal : C::no_intersection;
    }
}

void scheduler_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(net::names::scheduler, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(net::names::scheduler, address);
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&scheduler_t::on_model_update, coordinator);
                plugin->subscribe_actor(&scheduler_t::on_app_ready, coordinator);
                plugin->subscribe_actor(&scheduler_t::on_thread_ready, supervisor->get_address());
            }
        });
    });
}

void scheduler_t::on_thread_ready(model::message::thread_ready_t &message) noexcept {
    auto &p = message.payload;
    if (p.thread_id == std::this_thread::get_id()) {
        LOG_TRACE(log, "on_thread_ready");
        cluster = message.payload.cluster;
    }
}

void scheduler_t::on_app_ready(model::message::app_ready_t &) noexcept {
    LOG_TRACE(log, "on_app_ready");
    scan_next();
}

void scheduler_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto scheduler_t::operator()(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (!scan_in_progress) {
        scan_next_or_schedule();
    }
    return diff.visit_next(*this, custom);
}

auto scheduler_t::operator()(const model::diff::local::scan_request_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    bool updated = false;
    bool ignore = false;
    for (auto it = scan_queue.begin(); it != scan_queue.end();) {
        if (it->folder_id == diff.folder_id) {
            using C = subpath_comparison_t;
            auto r = compare(it->sub_dir, diff.sub_dir);
            if (r == C::equal || r == C::includes) {
                LOG_DEBUG(log, "ignored scan request of '{}' of '{}' ", diff.sub_dir, diff.folder_id, it->sub_dir);
                ignore = true;
            } else if (r == subpath_comparison_t::is_included) {
                ignore = true;
                updated = false;
                LOG_DEBUG(log, "updated scan request of '{}': '{}' -> '{}'", diff.folder_id, it->sub_dir, diff.sub_dir);
                it->sub_dir = diff.sub_dir;
            } else {
                // NOOP. will be included
            }
            break;
        } else {
            ++it;
        }
    }
    if (!ignore) {
        scan_queue.emplace_back(scan_item_t{diff.folder_id, diff.sub_dir});
    }
    if (!ignore || updated) {
        if (!scan_in_progress) {
            scan_next_or_schedule();
        }
    }
    return diff.visit_next(*this, custom);
}

auto scheduler_t::operator()(const model::diff::local::scan_finish_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    scan_in_progress = false;
    scan_next_or_schedule();
    return diff.visit_next(*this, custom);
}

auto scheduler_t::operator()(const model::diff::local::synchronization_finish_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (!scan_in_progress) {
        scan_next_or_schedule();
    }
    return diff.visit_next(*this, custom);
}

void scheduler_t::scan_next_or_schedule() noexcept {
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
        timer_id = start_timer(next->interval, *this, &scheduler_t::on_timer);
        schedule_option = next;
    }
}

auto scheduler_t::scan_next() noexcept -> schedule_option_t {
    while (!scan_queue.empty()) {
        auto item = std::move(scan_queue.front());
        scan_queue.pop_front();
        auto folder = cluster->get_folders().by_id(item.folder_id);
        if (!folder || folder->is_synchronizing() || ((folder->is_suspended() && !folder->get_suspend_reason()))) {
            continue;
        }
        for (auto it = scan_queue.begin(); it != scan_queue.end();) {
            if (it->folder_id == item.folder_id && it->sub_dir == item.sub_dir) {
                it = scan_queue.erase(it);
            } else {
                ++it;
            }
        }
        initiate_scan(item.folder_id, item.sub_dir);
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
        initiate_scan(folder->get_id(), {});
        return {};
    }
    if (folder) {
        auto folder_id = std::string(folder->get_id());
        auto item = scan_item_t{folder_id, {}};
        return next_schedule_t{std::move(item), deadline - now, deadline};
    }
    return {};
}

void scheduler_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    timer_id = {};
    if (!cancelled) {
        auto item = std::move(schedule_option->item);
        if (auto folder = cluster->get_folders().by_id(item.folder_id); folder) {
            bool do_scan = false;
            auto last_scan = folder->get_scan_finish();
            if (!last_scan.is_not_a_date_time()) {
                auto interval = folder->get_rescan_interval();
                if (interval <= 0) {
                    interval = 1;
                }
                auto interval_s = r::pt::seconds{interval};
                auto now = r::pt::microsec_clock::local_time();
                auto passed = now - last_scan;
                if (passed < interval_s) {
                    scan_next_or_schedule();
                } else {
                    do_scan = true;
                }
            }
            if (do_scan) {
                initiate_scan(item.folder_id, item.sub_dir);
            }
        }
    }
}

void scheduler_t::initiate_scan(std::string_view folder_id, std::string_view sub_dir) noexcept {
    LOG_DEBUG(log, "initiating folder '{}' scan (sub_dir: {})", folder_id, sub_dir);
    auto diff = model::diff::cluster_diff_ptr_t{};
    auto now = r::pt::microsec_clock::local_time();
    diff = new model::diff::local::scan_start_t(folder_id, sub_dir, now);
    send<model::payload::model_update_t>(coordinator, std::move(diff));
    scan_in_progress = true;
}
