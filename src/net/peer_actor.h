#pragma once

#include "../configuration.h"
#include "../proto/bep_support.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio/supervisor_asio.h>
#include <optional>
#include <list>

namespace syncspirit {

namespace net {

struct peer_actor_config_t : public r::actor_config_t {
    std::string_view device_name;
    model::device_id_t peer_device_id;
    model::peer_contact_t::uri_container_t uris;
    std::optional<tcp_socket_t> sock;
    const utils::key_pair_t *ssl_pair;
    config::bep_config_t bep_config;
};

template <typename Actor> struct peer_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&peer_device_id(const model::device_id_t &value) &&noexcept {
        parent_t::config.peer_device_id = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&uris(const model::peer_contact_t::uri_container_t &value) &&noexcept {
        parent_t::config.uris = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&ssl_pair(const utils::key_pair_t *value) &&noexcept {
        parent_t::config.ssl_pair = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&device_name(const std::string_view &value) &&noexcept {
        parent_t::config.device_name = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&bep_config(const config::bep_config_t &value) &&noexcept {
        parent_t::config.bep_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&sock(std::optional<tcp_socket_t> &&value) &&noexcept {
        parent_t::config.sock = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct peer_actor_t : public r::actor_base_t {
    using config_t = peer_actor_config_t;
    template <typename Actor> using config_builder_t = peer_actor_config_builder_t<Actor>;

    peer_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_start() noexcept override;

  private:
    struct confidential {
        struct payload {
            struct tx_item_t : r::arc_base_t<tx_item_t> {
                fmt::memory_buffer buff;
                bool final = false;

                tx_item_t(fmt::memory_buffer &&buff_, bool final_) noexcept : buff{std::move(buff_)}, final{final_} {}
                tx_item_t(tx_item_t &&other) = default;
            };
        };

        struct message {
            using tx_item_t = r::message_t<payload::tx_item_t>;
        };
    };

    using resolve_it_t = payload::address_response_t::resolve_results_t::iterator;
    using tx_item_t = r::intrusive_ptr_t<confidential::payload::tx_item_t>;
    using tx_message_t = confidential::message::tx_item_t;
    using tx_queue_t = std::list<tx_item_t>;
    using read_action_t = std::function<void(proto::message::message_t &&msg)>;

    void on_resolve(message::resolve_response_t &res) noexcept;
    void on_connect(resolve_it_t) noexcept;
    void on_io_error(const sys::error_code &ec) noexcept;
    void on_write(std::size_t bytes) noexcept;
    void on_read(std::size_t bytes) noexcept;
    void try_next_uri() noexcept;
    void initiate(transport::transport_sp_t tran, const utils::URI &url) noexcept;
    void on_handshake(bool valid_peer, X509 *peer_cert) noexcept;
    void on_handshake_error(sys::error_code ec) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;
    void read_more() noexcept;
    void push_write(fmt::memory_buffer &&buff, bool final) noexcept;
    void process_tx_queue() noexcept;
    void cancel_timer() noexcept;

    void read_hello(proto::message::message_t &&msg) noexcept;
    void read_cluster_config(proto::message::message_t &&msg) noexcept;

    std::string_view device_name;
    model::device_id_t peer_device_id;
    model::peer_contact_t::uri_container_t uris;
    std::optional<tcp_socket_t> sock;
    const utils::key_pair_t &ssl_pair;
    r::address_ptr_t resolver;
    transport::transport_sp_t transport;
    std::int32_t uri_idx = -1;
    std::optional<r::request_id_t> timer_request;
    tx_queue_t tx_queue;
    tx_item_t tx_item;
    fmt::memory_buffer rx_buff;
    std::size_t rx_idx = 0;
    bool valid_peer = false;
    read_action_t read_action;
};

} // namespace net
} // namespace syncspirit
