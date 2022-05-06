// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2022 Ivan Baidakou

#pragma once

#include "messages.h"
#include "utils/log.h"
#include "config/relay.h"
#include "proto/relay_support.h"
#include "transport/stream.h"
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <rotor/asio/supervisor_asio.h>
#include <optional>
#include <deque>

namespace syncspirit::net {

struct relay_actor_config_t : r::actor_config_t {
    model::cluster_ptr_t cluster;
    config::relay_config_t config;
};

template <typename Actor> struct relay_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&relay_config(const config::relay_config_t &value) &&noexcept {
        parent_t::config.config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API relay_actor_t : public r::actor_base_t {
    using config_t = relay_actor_config_t;
    template <typename Actor> using config_builder_t = relay_actor_config_builder_t<Actor>;

    relay_actor_t(config_t &config) noexcept;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;

  private:
    using http_rx_buff_t = payload::http_request_t::rx_buff_ptr_t;
    using request_option_t = std::optional<r::request_id_t>;
    using relays_t = proto::relay::relay_infos_t;
    using tx_item_t = boost::local_shared_ptr<std::string>;
    using tx_queue_t = std::deque<tx_item_t>;

    enum rx_state_t : std::uint32_t {
        response = 1 << 0,
        ping = 1 << 1,
        pong = 1 << 2,
        invitation = 1 << 3,
    };

    void request_relay_list() noexcept;
    void connect_to_relay() noexcept;
    void push_master(std::string data) noexcept;
    void write_master() noexcept;
    void read_master() noexcept;
    void respawn_ping_timer() noexcept;
    void respawn_tx_timer() noexcept;
    void respawn_rx_timer() noexcept;
    void send_ping() noexcept;

    void on_list(message::http_response_t &res) noexcept;
    void on_connect(message::connect_response_t &res) noexcept;
    void on_write(std::size_t sz) noexcept;
    void on_read(std::size_t bytes) noexcept;
    void on_io_error(const sys::error_code &ec, r::plugin::resource_id_t resource) noexcept;
    bool on(proto::relay::message_t &) noexcept;
    bool on(proto::relay::response_t &) noexcept;
    bool on(proto::relay::session_invitation_t &) noexcept;
    void on_ping_timer(r::request_id_t, bool cancelled) noexcept;
    void on_tx_timer(r::request_id_t, bool cancelled) noexcept;
    void on_rx_timer(r::request_id_t, bool cancelled) noexcept;

    model::cluster_ptr_t cluster;
    config::relay_config_t config;
    utils::logger_t log;

    r::address_ptr_t http_client;
    r::address_ptr_t coordinator;
    r::address_ptr_t peer_supervisor;
    http_rx_buff_t http_rx_buff;
    request_option_t http_request;
    relays_t relays;
    int relay_index = -1;
    transport::stream_sp_t master;
    fmt::memory_buffer rx_buff;
    std::size_t rx_idx = 0;
    std::uint32_t rx_state = 0;
    tx_queue_t tx_queue;
    std::optional<r::request_id_t> ping_timer;
    std::optional<r::request_id_t> tx_timer;
    std::optional<r::request_id_t> rx_timer;
};

} // namespace syncspirit::net
