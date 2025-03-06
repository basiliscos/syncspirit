// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <rotor.hpp>
#include "model/cluster.h"
#include "config/bep.h"
#include "messages.h"
#include "utils/log.h"
#include "transport/stream.h"

namespace syncspirit::net {

namespace r = rotor;

struct initiator_actor_config_t : public r::actor_config_t {
    model::device_id_t peer_device_id;
    utils::uri_container_t uris;
    utils::bytes_t relay_session;
    const utils::key_pair_t *ssl_pair;
    std::optional<tcp_socket_t> sock;
    model::cluster_ptr_t cluster;
    r::address_ptr_t sink;
    r::message_ptr_t custom;
    r::supervisor_t *router;
    std::string_view alpn;
    bool relay_enabled;
};

template <typename Actor> struct initiator_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&peer_device_id(const model::device_id_t &value) && noexcept {
        parent_t::config.peer_device_id = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&uris(const utils::uri_container_t &value) && noexcept {
        parent_t::config.uris = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&relay_session(utils::bytes_view_t value) && noexcept {
        parent_t::config.relay_session = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&ssl_pair(const utils::key_pair_t *value) && noexcept {
        parent_t::config.ssl_pair = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&sock(tcp_socket_t value) && noexcept {
        parent_t::config.sock = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&sink(const r::address_ptr_t &value) && noexcept {
        parent_t::config.sink = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&custom(r::message_ptr_t value) && noexcept {
        parent_t::config.custom = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&router(r::supervisor_t &value) && noexcept {
        parent_t::config.router = &value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&alpn(std::string_view value) && noexcept {
        parent_t::config.alpn = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&relay_enabled(bool value) && noexcept {
        parent_t::config.relay_enabled = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API initiator_actor_t : r::actor_base_t {
    using config_t = initiator_actor_config_t;
    template <typename Actor> using config_builder_t = initiator_actor_config_builder_t<Actor>;

    initiator_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;

    template <typename T> auto &access() noexcept;

  private:
    using resolve_it_t = payload::address_response_t::resolve_results_t::iterator;
    enum class role_t { active, passive, relay_passive };

    void initiate_passive() noexcept;
    void initiate_active() noexcept;
    void initiate_relay_passive() noexcept;
    void initiate_active_tls(const utils::uri_ptr_t &uri) noexcept;
    void initiate_active_relay(const utils::uri_ptr_t &uri) noexcept;
    void initiate_handshake() noexcept;
    void join_session() noexcept;
    void request_relay_connection() noexcept;
    void resolve(const utils::uri_ptr_t &uri) noexcept;

    void on_resolve(message::resolve_response_t &res) noexcept;
    void on_connect(const tcp::endpoint &) noexcept;
    void on_io_error(const sys::error_code &ec, r::plugin::resource_id_t resource) noexcept;
    void on_handshake(bool valid_peer, utils::x509_t &peer_cert, const tcp::endpoint &peer_endpoint,
                      const model::device_id_t *peer_device) noexcept;
    void on_read_relay(size_t bytes) noexcept;
    void on_read_relay_active(size_t bytes) noexcept;
    void on_write(size_t bytes) noexcept;

    model::device_id_t peer_device_id;
    utils::uri_container_t uris;
    utils::bytes_t relay_rx;
    utils::bytes_t relay_tx;
    utils::bytes_t relay_key;
    const utils::key_pair_t &ssl_pair;
    std::optional<tcp_socket_t> sock;
    model::cluster_ptr_t cluster;
    r::address_ptr_t sink;
    r::message_ptr_t custom;
    r::supervisor_t &router;
    std::string_view alpn;

    utils::uri_ptr_t active_uri;
    transport::stream_sp_t transport;
    r::address_ptr_t resolver;
    r::address_ptr_t coordinator;
    size_t uri_idx = 0;
    utils::logger_t log;
    tcp::endpoint remote_endpoint;
    bool connected = false;
    role_t role = role_t::passive;
    utils::bytes_t rx_buff;
    bool success = false;
    bool relaying = false;
};

namespace payload {

struct peer_connected_t {
    transport::stream_sp_t transport;
    model::device_id_t peer_device_id;
    tcp::endpoint remote_endpoint;
    std::string proto;
    r::message_ptr_t custom;
};

} // namespace payload

namespace message {

using peer_connected_t = r::message_t<payload::peer_connected_t>;

}

} // namespace syncspirit::net
