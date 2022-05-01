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
    const utils::key_pair_t *ssl_pair;
    std::optional<tcp_socket_t> sock;
    model::cluster_ptr_t cluster;
};

template <typename Actor> struct initiator_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&peer_device_id(const model::device_id_t &value) &&noexcept {
        parent_t::config.peer_device_id = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&uris(const utils::uri_container_t &value) &&noexcept {
        parent_t::config.uris = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&ssl_pair(const utils::key_pair_t *value) &&noexcept {
        parent_t::config.ssl_pair = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&sock(tcp_socket_t value) &&noexcept {
        parent_t::config.sock = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) &&noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct initiator_actor_t : r::actor_base_t {
    using config_t = initiator_actor_config_t;
    template <typename Actor> using config_builder_t = initiator_actor_config_builder_t<Actor>;

    initiator_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;

  private:
    using resolve_it_t = payload::address_response_t::resolve_results_t::iterator;

    void initiate_passive() noexcept;
    void initiate_active() noexcept;
    void initiate(transport::stream_sp_t stream, const utils::URI &uri) noexcept;
    void initiate_handshake() noexcept;

    void on_resolve(message::resolve_response_t &res) noexcept;
    void on_connect(resolve_it_t) noexcept;
    void on_io_error(const sys::error_code &ec, r::plugin::resource_id_t resource) noexcept;
    void on_handshake(bool valid_peer, utils::x509_t &peer_cert, const tcp::endpoint &peer_endpoint,
                      const model::device_id_t *peer_device) noexcept;

    model::device_id_t peer_device_id;
    utils::uri_container_t uris;
    const utils::key_pair_t &ssl_pair;
    std::optional<tcp_socket_t> sock;
    model::cluster_ptr_t cluster;

    transport::stream_sp_t transport;
    r::address_ptr_t resolver;
    r::address_ptr_t coordinator;
    size_t uri_idx = 0;
    utils::logger_t log;
    tcp::endpoint remote_endpoint;
    bool connected = false;
    bool active = false;
    bool success = false;
};

namespace payload {

struct peer_connected_t {
    transport::stream_sp_t transport;
    model::device_id_t peer_device_id;
    tcp::endpoint remote_endpoint;
};

} // namespace payload

namespace message {

using peer_connected_t = r::message_t<payload::peer_connected_t>;

}

} // namespace syncspirit::net