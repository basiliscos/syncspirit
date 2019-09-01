#pragma once

#include "../configuration.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <rotor/asio/supervisor_asio.h>

namespace syncspirit {
namespace net {

namespace ssl = boost::asio::ssl;

struct global_discovery_actor_t : public r::actor_base_t {
    using resolve_results_t = asio::ip::tcp::resolver::results_type;
    using resolve_it_t = asio::ip::tcp::resolver::results_type::iterator;

    global_discovery_actor_t(ra::supervisor_asio_t &sup, const config::global_announce_config_t &cfg);

    virtual void on_initialize(r::message::init_request_t &) noexcept override;
    virtual void on_start(r::message_t<r::payload::start_actor_t> &) noexcept override;
    virtual void on_shutdown(r::message::shutdown_request_t &) noexcept override;

    void trigger_shutdown() noexcept;
    void on_timeout_trigger() noexcept;
    void on_timeout_error(const sys::error_code &ec) noexcept;
    void on_resolve_error(const sys::error_code &ec) noexcept;
    void on_resolve(resolve_results_t results) noexcept;
    void on_connect_error(const sys::error_code &ec) noexcept;
    void on_connect(resolve_it_t endpoint) noexcept;
    void on_handshake_error(const sys::error_code &ec) noexcept;
    void on_handshake() noexcept;

  private:
    using endpoint_t = tcp::endpoint;
    using stream_t = ssl::stream<tcp_socket_t>;

    const static constexpr std::uint32_t SHUTDOWN_ACTIVE = 0b0000'0001;
    const static constexpr std::uint32_t TIMER_ACTIVE = 0b0000'0010;
    const static constexpr std::uint32_t RESOLVER_ACTIVE = 0b0000'0100;
    const static constexpr std::uint32_t SOCKET_ACTIVE = 0b0000'1000;

    config::global_announce_config_t cfg;
    asio::io_context::strand &strand;
    asio::io_context &io_context;
    ssl::context ssl_context;
    tcp::resolver resolver;
    stream_t stream;
    timer_t timer;
    std::uint32_t activities_flag;
};

} // namespace net
} // namespace syncspirit
