// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#pragma once

#include "model/cluster.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "utils/log.h"
#include <rotor.hpp>
#include <optional>
#include <list>

namespace syncspirit {
namespace net {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API scheduler_t : public r::actor_base_t, private model::diff::cluster_visitor_t {
    using parent_t = r::actor_base_t;
    using parent_t::parent_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

    template <typename T> auto &access() noexcept;

  private:
    struct scan_item_t {
        std::string folder_id;
        std::string sub_dir;
    };
    struct next_schedule_t {
        scan_item_t item;
        r::pt::time_duration interval;
        r::pt::ptime at;
    };

    using schedule_option_t = std::optional<next_schedule_t>;
    using scan_queue_t = std::list<scan_item_t>;

    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_thread_ready(model::message::thread_ready_t &) noexcept;
    void on_app_ready(model::message::app_ready_t &) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;

    schedule_option_t scan_next() noexcept;
    void scan_next_or_schedule() noexcept;
    void initiate_scan(std::string_view folder_id, std::string_view sub_dir) noexcept;

    outcome::result<void> operator()(const model::diff::modify::upsert_folder_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const model::diff::local::scan_request_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const model::diff::local::scan_finish_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const model::diff::local::synchronization_finish_t &,
                                     void *custom) noexcept override;

    model::cluster_ptr_t cluster;
    scan_queue_t scan_queue;
    utils::logger_t log;
    r::address_ptr_t coordinator;
    std::optional<r::request_id_t> timer_id;
    schedule_option_t schedule_option;
    bool scan_in_progress = false;
};

} // namespace net
} // namespace syncspirit
