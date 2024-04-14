// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "messages.h"
#include "utils/log.h"
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <rotor/asio/supervisor_asio.h>

namespace syncspirit {
namespace net {

struct acceptor_actor_config_t : r::actor_config_t {
    model::cluster_ptr_t cluster;
};

template <typename Actor> struct acceptor_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API acceptor_actor_t : public r::actor_base_t {
    using config_t = acceptor_actor_config_t;
    template <typename Actor> using config_builder_t = acceptor_actor_config_builder_t<Actor>;

    acceptor_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_start() noexcept override;
    void on_start() noexcept override;

  private:
    using tcp_socket_option_t = boost::optional<tcp_socket_t>;

    void accept_next() noexcept;
    void on_accept(const sys::error_code &ec) noexcept;

    utils::logger_t log;
    asio::io_context::strand &strand;
    tcp::endpoint endpoint;
    tcp::acceptor acceptor;
    tcp_socket_t peer;
    r::address_ptr_t coordinator;
    model::cluster_ptr_t cluster;
};

} // namespace net
} // namespace syncspirit
