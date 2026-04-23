// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "config/dialer.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "messages.h"
#include "model_actor.hpp"
#include <optional>
#include <unordered_map>
#include <chrono>

namespace syncspirit {
namespace net {

namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API dialer_actor_t final : public model_actor_t<r::actor_base_t>,
                                             private model::diff::cluster_visitor_t {
    using parent_t = model_actor_t<r::actor_base_t>;

    struct config_t : parent_t::config_t {
        using base_t = model_actor_t<r::actor_base_t>::config_t;
        using base_t::base_t;
        config::dialer_config_t dialer_config;
    };

    template <typename Actor> struct config_builder_t : parent_t::template config_builder_t<Actor> {
        using builder_t = typename Actor::template config_builder_t<Actor>;
        using base_t = parent_t::template config_builder_t<Actor>;
        using base_t::base_t;

        builder_t &&dialer_config(const config::dialer_config_t &value) && noexcept {
            base_t::config.dialer_config = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
    };

    explicit dialer_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;
    void on_start() noexcept override;

  private:
    using clock_t = std::chrono::steady_clock;
    using timer_option_t = std::optional<rotor::request_id_t>;
    struct redial_info_t {
        clock_t::time_point last_attempt;
        timer_option_t timer_id;
        std::size_t skip_discovers;
    };
    using redial_map_t = std::unordered_map<model::device_ptr_t, redial_info_t>;

    void on_announce(message::announce_notification_t &message) noexcept;
    void visit(const model::diff::cluster_diff_t &, model::payload::apply_context_t &) noexcept override;
    void post_configure_coordinator() noexcept override;

    void discover_or_dial(const model::device_ptr_t &device) noexcept;
    void remove(const model::device_ptr_t &device) noexcept;
    void schedule_redial(const model::device_ptr_t &device) noexcept;
    void on_timer(r::request_id_t request_id, bool cancelled) noexcept;

    outcome::result<void> operator()(const model::diff::contact::peer_state_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::contact::update_contact_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::remove_peer_t &, void *) noexcept override;

    pt::time_duration redial_timeout;
    std::uint32_t skip_discovers;

    redial_map_t redial_map;
    bool announced;
};

} // namespace net
} // namespace syncspirit
