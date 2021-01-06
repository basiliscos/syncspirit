#pragma once

#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio/supervisor_asio.h>
#include <optional>

namespace syncspirit {
namespace net {

struct ssdp_actor_config_t : r::actor_config_t {
    std::uint32_t max_wait;
};

template <typename Actor> struct ssdp_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&max_wait(std::uint32_t value) &&noexcept {
        parent_t::config.max_wait = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct ssdp_actor_t : public r::actor_base_t {
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

    asio::io_context::strand &strand;
    std::optional<r::request_id_t> timer_request;
    std::uint32_t max_wait;
    std::unique_ptr<udp_socket_t> sock;
    r::address_ptr_t coordinator_addr;

    fmt::memory_buffer tx_buff;
    fmt::memory_buffer rx_buff;
};

} // namespace net
} // namespace syncspirit
