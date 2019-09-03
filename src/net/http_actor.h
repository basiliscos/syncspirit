#pragma once

#include "../configuration.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor.hpp>

namespace syncspirit {
namespace net {

struct http_actor_t : public r::actor_base_t {
    using tcp_socket_ptr_t = std::unique_ptr<tcp_socket_t>;
    using resolve_results_t = tcp::resolver::results_type;
    using resolve_it_t = resolve_results_t::iterator;
    using request_ptr_t = r::intrusive_ptr_t<message::http_request_t>;

    http_actor_t(ra::supervisor_asio_t &sup);

    virtual void on_initialize(r::message::init_request_t &) noexcept override;
    virtual void on_shutdown(r::message::shutdown_request_t &) noexcept override;
    virtual void on_start(r::message_t<r::payload::start_actor_t> &) noexcept override;
    virtual void on_request(message::http_request_t &) noexcept;

    void trigger_request() noexcept;
    void on_resolve_error(const sys::error_code &ec) noexcept;
    void on_resolve(resolve_results_t results) noexcept;
    void on_connect(resolve_it_t endpoint) noexcept;
    void on_tcp_error(const sys::error_code &ec) noexcept;
    void on_request_sent(std::size_t bytes) noexcept;
    void on_response_received(std::size_t bytes) noexcept;

  private:
    const static constexpr std::uint32_t SHUTDOWN_ACTIVE = 1 << 0;
    const static constexpr std::uint32_t TCP_ACTIVE = 1 << 1;
    const static constexpr std::uint32_t RESOLVER_ACTIVE = 1 << 2;

    void clean_state() noexcept;
    void reply_error(const sys::error_code &ec) noexcept;

    asio::io_context::strand &strand;
    asio::io_context &io_context;
    tcp::resolver resolver;
    std::uint32_t activities_flag;

    tcp_socket_ptr_t sock;
    tcp::endpoint local_endpoint;
    request_ptr_t request;
    http::response<http::string_body> response;
};

} // namespace net
} // namespace syncspirit
