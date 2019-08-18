#pragma once

#include "../configuration.h"
#include "../utils/upnp_support.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio/supervisor_asio.h>

namespace syncspirit {
namespace net {

struct ssdp_actor_t : public r::actor_base_t {

    ssdp_actor_t(ra::supervisor_asio_t &sup, std::uint32_t max_wait);
    virtual void on_initialize(r::message_t<r::payload::initialize_actor_t> &) noexcept override;
    virtual void on_start(r::message_t<r::payload::start_actor_t> &) noexcept override;
    virtual void on_shutdown(r::message_t<r::payload::shutdown_request_t> &) noexcept override;
    virtual void on_try_again(r::message_t<try_again_request_t> &) noexcept;

    void trigger_shutdown() noexcept;
    void cancel_pending() noexcept;
    void reply_error(const sys::error_code &ec) noexcept;

    void on_timeout_trigger() noexcept;
    void on_timeout_error(const sys::error_code &ec) noexcept;
    void on_discovery_sent(std::size_t bytes) noexcept;
    void on_udp_error(const sys::error_code &ec) noexcept;
    void on_discovery_received(std::size_t bytes) noexcept;

  private:
    const static constexpr std::uint32_t SHUTDOWN_ACTIVE = 1 << 0;
    const static constexpr std::uint32_t TIMER_ACTIVE = 1 << 1;
    const static constexpr std::uint32_t UDP_SEND = 1 << 2;
    const static constexpr std::uint32_t UDP_RECV = 1 << 3;

    void initate_discovery() noexcept;

    asio::io_context::strand &strand;
    asio::io_context &io_context;
    timer_t timer;
    udp_socket_t sock;
    std::uint32_t max_wait;
    std::uint32_t activities_flag;

    fmt::memory_buffer tx_buff;
    fmt::memory_buffer rx_buff;
};

} // namespace net
} // namespace syncspirit
