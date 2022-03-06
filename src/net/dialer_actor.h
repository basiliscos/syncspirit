// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "config/dialer.h"
#include "config/bep.h"
#include "utils/log.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "messages.h"
#include <optional>
#include <unordered_map>
#include <chrono>

namespace syncspirit {
namespace net {

namespace outcome = boost::outcome_v2;

struct dialer_actor_config_t : r::actor_config_t {
    config::dialer_config_t dialer_config;
    model::cluster_ptr_t cluster;
};

template <typename Actor> struct dialer_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&dialer_config(const config::dialer_config_t &value) &&noexcept {
        parent_t::config.dialer_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct dialer_actor_t : public r::actor_base_t, private model::diff::cluster_visitor_t {
    using config_t = dialer_actor_config_t;
    template <typename Actor> using config_builder_t = dialer_actor_config_builder_t<Actor>;

    explicit dialer_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;
    void on_start() noexcept override;

  private:
    using clock_t = std::chrono::steady_clock;
    using redial_map_t = std::unordered_map<model::device_ptr_t, rotor::request_id_t>;
    void on_announce(message::announce_notification_t &message) noexcept;
    void on_model_update(model::message::model_update_t &) noexcept;

    void discover(const model::device_ptr_t &device) noexcept;
    void remove(const model::device_ptr_t &device) noexcept;
    void schedule_redial(const model::device_ptr_t &device) noexcept;
    void on_timer(r::request_id_t request_id, bool cancelled) noexcept;

    outcome::result<void> operator()(const model::diff::peer::peer_state_t &) noexcept override;

    utils::logger_t log;
    model::cluster_ptr_t cluster;
    pt::time_duration redial_timeout;

    r::address_ptr_t coordinator;
    redial_map_t redial_map;
};

} // namespace net
} // namespace syncspirit
