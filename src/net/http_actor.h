#pragma once

#include "../configuration.h"
#include "../transport/base.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor.hpp>
#include <variant>

namespace syncspirit {
namespace net {

struct http_actor_config_t : public r::actor_config_t {
    using r::actor_config_t::actor_config_t;
    r::pt::time_duration resolve_timeout;
    r::pt::time_duration request_timeout;
    std::string registry_name;
    bool keep_alive;
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

    builder_t &&registry_name(const std::string &value) &&noexcept {
        parent_t::config.registry_name = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&keep_alive(bool value = true) &&noexcept {
        parent_t::config.keep_alive = value;
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
    using queue_t = std::list<request_ptr_t>;

    void process() noexcept;
    void spawn_timer() noexcept;
    void on_request(message::http_request_t &req) noexcept;
    void on_resolve(message::resolve_response_t &res) noexcept;
    void on_connect(resolve_it_t) noexcept;
    void on_request_sent(std::size_t /* bytes */) noexcept;
    void on_request_read(std::size_t bytes) noexcept;
    void on_tcp_error(const sys::error_code &ec) noexcept;
    void on_timer_error(const sys::error_code &ec) noexcept;
    void on_timer_trigger() noexcept;
    void on_shutdown_timer_error(const sys::error_code &ec) noexcept;
    void on_shutdown_timer_trigger() noexcept;
    void on_handshake(bool valid_peer) noexcept;
    void on_handshake_error(sys::error_code ec) noexcept;
    void cancel_sock() noexcept;
    void cancel_timer() noexcept;
    void cancel_io() noexcept;
    void write_request() noexcept;
    void start_shutdown_timer() noexcept;

    pt::time_duration resolve_timeout;
    pt::time_duration request_timeout;
    std::string registry_name;
    bool keep_alive;
    asio::io_context::strand &strand;
    asio::deadline_timer request_timer;
    asio::deadline_timer shutdown_timer;
    r::address_ptr_t resolver;
    queue_t queue;
    bool need_response = false;
    bool stop_io = false;
    transport::transport_sp_t transport;
    transport::http_base_t *http_adapter = nullptr;
    http::request<http::empty_body> http_request;
    http::response<http::string_body> http_response;
    size_t response_size = 0;
    utils::URI resolved_url;
};

} // namespace net
} // namespace syncspirit
