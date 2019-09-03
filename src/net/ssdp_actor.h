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
    virtual void init_start() noexcept override;
    virtual void shutdown_start() noexcept override;
    void on_request(message::ssdp_request_t &) noexcept;

    void cancel_pending() noexcept;
    void reply_error(const sys::error_code &ec) noexcept;

    void on_discovery_sent(std::size_t bytes) noexcept;
    void on_udp_error(const sys::error_code &ec) noexcept;
    void on_discovery_received(std::size_t bytes) noexcept;

  private:
    const static constexpr std::uint32_t UDP_SEND = 1 << 0;
    const static constexpr std::uint32_t UDP_RECV = 1 << 1;

    void initate_discovery() noexcept;

    asio::io_context::strand &strand;
    asio::io_context &io_context;
    std::unique_ptr<udp_socket_t> sock;
    std::uint32_t max_wait;
    std::uint32_t activities_flag;

    fmt::memory_buffer tx_buff;
    fmt::memory_buffer rx_buff;

    r::intrusive_ptr_t<message::ssdp_request_t> request;
};

} // namespace net
} // namespace syncspirit
