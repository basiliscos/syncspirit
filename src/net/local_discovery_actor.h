#pragma once

#include "messages.h"
#include <boost/asio.hpp>
#include <optional>

namespace syncspirit {
namespace net {

struct local_discovery_actor_config_t : r::actor_config_t {
    std::uint16_t port;
    std::uint32_t frequency;
    model::device_ptr_t device;
};

template <typename Actor> struct local_discovery_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&device(const model::device_ptr_t &value) &&noexcept {
        parent_t::config.device = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&port(const std::uint16_t value) &&noexcept {
        parent_t::config.port = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&frequency(const std::uint32_t value) &&noexcept {
        parent_t::config.frequency = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct local_discovery_actor_t : public r::actor_base_t {
    using config_t = local_discovery_actor_config_t;
    template <typename Actor> using config_builder_t = local_discovery_actor_config_builder_t<Actor>;

    explicit local_discovery_actor_t(config_t &cfg);

    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;

  private:
    using discovery_request_t = r::intrusive_ptr_t<message::discovery_request_t>;
    using discovery_queue_t = std::list<discovery_request_t>;

    void init() noexcept;
    void announce() noexcept;
    void do_read() noexcept;

    void on_endpoint(message::endpoint_response_t &res) noexcept;

    void on_read(size_t bytes) noexcept;
    void on_read_error(const sys::error_code &ec) noexcept;
    void on_write(size_t bytes) noexcept;
    void on_write_error(const sys::error_code &ec) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;

    r::address_ptr_t acceptor;
    r::pt::time_duration frequency;
    model::device_ptr_t device;
    asio::io_context::strand &strand;
    udp_socket_t sock;
    r::address_ptr_t coordinator;
    fmt::memory_buffer rx_buff;
    fmt::memory_buffer tx_buff;
    std::vector<utils::URI> uris;
    udp::endpoint bc_endpoint;
    udp::endpoint peer_endpoint;
    std::optional<r::request_id_t> timer_request;
    std::optional<r::request_id_t> endpoint_request;
};

} // namespace net
} // namespace syncspirit
