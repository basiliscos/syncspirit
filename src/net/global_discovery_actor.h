// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "messages.h"
#include "model/messages.h"
#include "model/diff/contact_visitor.h"
#include "utils/log.h"
#include <boost/asio.hpp>
#include <optional>
#include <unordered_set>

namespace syncspirit {
namespace net {

namespace outcome = boost::outcome_v2;

struct global_discovery_actor_config_t : r::actor_config_t {
    utils::URI announce_url;
    model::device_id_t device_id;
    const utils::key_pair_t *ssl_pair;
    std::uint32_t rx_buff_size;
    std::uint32_t io_timeout;
    model::cluster_ptr_t cluster;
};

template <typename Actor> struct global_discovery_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&announce_url(const utils::URI &value) &&noexcept {
        parent_t::config.announce_url = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&ssl_pair(const utils::key_pair_t *value) &&noexcept {
        parent_t::config.ssl_pair = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&rx_buff_size(const std::uint32_t value) &&noexcept {
        parent_t::config.rx_buff_size = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&io_timeout(const std::uint32_t value) &&noexcept {
        parent_t::config.io_timeout = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&device_id(const model::device_id_t &value) &&noexcept {
        parent_t::config.device_id = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct global_discovery_actor_t : public r::actor_base_t, private model::diff::contact_visitor_t {
    using config_t = global_discovery_actor_config_t;
    template <typename Actor> using config_builder_t = global_discovery_actor_config_builder_t<Actor>;

    explicit global_discovery_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    using rx_buff_t = payload::http_request_t::rx_buff_ptr_t;
    using discovering_devices_t = std::unordered_set<std::string>;
    using uris_t = std::unordered_set<std::string>;

    void announce() noexcept;
    void on_announce_response(message::http_response_t &message) noexcept;
    void on_discovery_response(message::http_response_t &message) noexcept;
    void on_contact_update(model::message::contact_update_t &message) noexcept;
    void on_discovery(message::discovery_notify_t &req) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;
    void make_request(const r::address_ptr_t &addr, utils::URI &uri, fmt::memory_buffer &&tx_buff,
                      const r::message_ptr_t &custom = {}) noexcept;

    outcome::result<void> operator()(const model::diff::modify::update_contact_t &) noexcept override;

    model::device_id_t device_id;
    utils::logger_t log;
    r::address_ptr_t http_client;
    r::address_ptr_t coordinator;
    utils::URI announce_url;
    model::device_id_t dicovery_device_id;
    const utils::key_pair_t &ssl_pair;
    rx_buff_t rx_buff;
    std::uint32_t rx_buff_size;
    std::uint32_t io_timeout;
    bool announced = false;
    r::address_ptr_t addr_announce;  /* for routing */
    r::address_ptr_t addr_discovery; /* for routing */
    std::optional<r::request_id_t> timer_request;
    std::optional<r::request_id_t> http_request;
    discovering_devices_t discovering_devices;
    model::cluster_ptr_t cluster;
    uris_t announced_uris;
};

} // namespace net
} // namespace syncspirit
