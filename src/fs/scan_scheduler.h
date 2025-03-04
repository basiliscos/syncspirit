// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/cluster.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "utils/log.h"
#include <rotor.hpp>
#include <optional>
#include <list>

namespace syncspirit {
namespace fs {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API scan_scheduler_config_t : r::actor_config_t {
    model::cluster_ptr_t cluster;
};

template <typename Actor> struct scan_scheduler_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API scan_scheduler_t : public r::actor_base_t, private model::diff::cluster_visitor_t {
    using config_t = scan_scheduler_config_t;
    template <typename Actor> using config_builder_t = scan_scheduler_config_builder_t<Actor>;

    explicit scan_scheduler_t(config_t &cfg);

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_ui_ready(model::message::ui_ready_t &) noexcept;

  private:
    struct next_schedule_t {
        r::pt::time_duration interval;
        r::pt::ptime at;
        std::string folder_id;
    };
    using schedule_option_t = std::optional<next_schedule_t>;
    using scan_queue_t = std::list<std::string>;

    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;

    schedule_option_t scan_next() noexcept;
    void scan_next_or_schedule() noexcept;
    void initiate_scan(std::string_view folder_id) noexcept;

    outcome::result<void> operator()(const model::diff::modify::upsert_folder_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const model::diff::local::scan_request_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const model::diff::local::scan_finish_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const model::diff::local::synchronization_finish_t &,
                                     void *custom) noexcept override;

    model::cluster_ptr_t cluster;
    scan_queue_t scan_queue;
    utils::logger_t log;
    r::address_ptr_t fs_scanner;
    r::address_ptr_t coordinator;
    std::optional<r::request_id_t> timer_id;
    schedule_option_t schedule_option;
    bool scan_in_progress;
};

} // namespace fs
} // namespace syncspirit
