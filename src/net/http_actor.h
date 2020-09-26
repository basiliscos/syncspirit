#pragma once

#include "../configuration.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor.hpp>
#include <variant>

namespace syncspirit {
namespace net {

struct http_actor_config_t : public r::actor_config_t {
    r::pt::time_duration resolve_timeout;
    r::pt::time_duration request_timeout;
    using r::actor_config_t::actor_config_t;
};

template <typename Actor> struct http_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&resolve_timeout(const pt::time_duration &value) &&noexcept {
        parent_t::config.resolve_timeout = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&request_timeout(const pt::time_duration &value) &&noexcept {
        parent_t::config.request_timeout = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};


struct http_actor_t : public r::actor_base_t {
    using request_ptr_t = r::intrusive_ptr_t<message::http_request_t>;
    using socket_ptr_t = std::unique_ptr<tcp::socket>;
    using secure_socket_t = std::unique_ptr<ssl::stream<tcp_socket_t>>;

    using resolve_it_t = payload::address_response_t::resolve_results_t::iterator;

    using config_t = http_actor_config_t;
    template <typename Actor> using config_builder_t = http_actor_config_builder_t<Actor>;

    explicit http_actor_t(config_t &config);

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
private:
    bool maybe_shutdown() noexcept;
    void on_request(message::http_request_t &req) noexcept;
    void on_resolve(message::resolve_response_t &res) noexcept;
    void on_connect(resolve_it_t) noexcept;
    void on_request_sent(std::size_t /* bytes */) noexcept;
    void on_request_read(std::size_t bytes) noexcept;
    void on_tcp_error(const sys::error_code &ec) noexcept;
    void on_timer_error(const sys::error_code &ec) noexcept;
    void on_timer_trigger() noexcept;
    void on_handshake() noexcept;
    void on_handshake_error(const sys::error_code &ec) noexcept;
    bool cancel_sock() noexcept;
    bool cancel_timer() noexcept;
    void write_request() noexcept;

    pt::time_duration resolve_timeout;
    pt::time_duration request_timeout;
    asio::io_context::strand &strand;
    asio::deadline_timer timer;
    r::address_ptr_t resolver;
    request_ptr_t orig_req;
    bool need_response = false;
    socket_ptr_t sock;
    secure_socket_t sock_s;
    http::request<http::empty_body> http_request;
    http::response<http::string_body> http_response;
    size_t response_size = 0;
};

} // namespace net
} // namespace syncspirit
