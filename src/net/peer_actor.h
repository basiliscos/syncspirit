#pragma once

#include "../configuration.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio/supervisor_asio.h>
#include <optional>

namespace syncspirit {
namespace net {

struct peer_actor_config_t : public r::actor_config_t {
    std::string_view device_name;
    model::device_id_t peer_device_id;
    model::peer_contact_t contact;
    const utils::key_pair_t *ssl_pair;
};

template <typename Actor> struct peer_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&peer_device_id(const model::device_id_t &value) &&noexcept {
        parent_t::config.peer_device_id = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&contact(const model::peer_contact_t &value) &&noexcept {
        parent_t::config.contact = value;
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
};

struct peer_actor_t : public r::actor_base_t {
    using config_t = peer_actor_config_t;
    template <typename Actor> using config_builder_t = peer_actor_config_builder_t<Actor>;

    peer_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    // void shutdown_start() noexcept override;

    model::device_id_t device_id;

  private:
    using resolve_it_t = payload::address_response_t::resolve_results_t::iterator;

    asio::mutable_buffer prepare_rx_buff() noexcept;
    void on_resolve(message::resolve_response_t &res) noexcept;
    void on_connect(resolve_it_t) noexcept;
    void on_io_error(const sys::error_code &ec) noexcept;
    void on_write(std::size_t bytes) noexcept;
    void on_read(std::size_t bytes) noexcept;
    void try_next_uri() noexcept;
    void initiate(transport::transport_sp_t tran, const utils::URI &url) noexcept;
    void on_handshake(bool valid_peer) noexcept;
    void on_handshake_error(sys::error_code ec) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;

    std::string_view device_name;
    model::peer_contact_t contact;
    const utils::key_pair_t &ssl_pair;
    r::address_ptr_t resolver;
    transport::transport_sp_t transport;
    std::int32_t uri_idx = -1;
    std::optional<r::request_id_t> timer_request;
    fmt::memory_buffer tx_buff;
    fmt::memory_buffer rx_buff;
};

} // namespace net
} // namespace syncspirit
