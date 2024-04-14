// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "messages.h"
#include "config/upnp.h"
#include "utils/log.h"
#include <boost/asio.hpp>
#include <rotor/asio/supervisor_asio.h>
#include <optional>

namespace syncspirit {
namespace net {

struct ssdp_actor_config_t : r::actor_config_t {
    config::upnp_config_t upnp_config;
    model::cluster_ptr_t cluster;
};

template <typename Actor> struct ssdp_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&upnp_config(const config::upnp_config_t &value) && noexcept {
        parent_t::config.upnp_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API ssdp_actor_t : public r::actor_base_t {
    using config_t = ssdp_actor_config_t;
    template <typename Actor> using config_builder_t = ssdp_actor_config_builder_t<Actor>;

    explicit ssdp_actor_t(ssdp_actor_config_t &cfg);

    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    void on_discovery_sent(std::size_t bytes) noexcept;
    void on_udp_send_error(const sys::error_code &ec) noexcept;
    void on_udp_recv_error(const sys::error_code &ec) noexcept;
    void on_discovery_received(std::size_t bytes) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;
    void timer_cancel() noexcept;
    void launch_upnp(const utils::URI &igd_uri) noexcept;

    model::cluster_ptr_t cluster;
    utils::logger_t log;
    asio::io_context::strand &strand;
    std::optional<r::request_id_t> timer_request;
    std::unique_ptr<udp_socket_t> sock;
    config::upnp_config_t upnp_config;
    r::address_ptr_t coordinator;
    udp::endpoint upnp_endpoint;

    fmt::memory_buffer tx_buff;
    fmt::memory_buffer rx_buff;
};

} // namespace net
} // namespace syncspirit
