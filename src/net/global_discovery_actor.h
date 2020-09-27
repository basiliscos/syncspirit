#pragma once

#include "../configuration.h"
#include "messages.h"
#include <boost/asio.hpp>

namespace syncspirit {
namespace net {

struct global_discovery_actor_config_t : r::actor_config_t {
    tcp::endpoint endpoint;
    utils::URI announce_url;
    std::string cert_file;
    std::string key_file;
    std::uint32_t rx_buff_size;
};

template <typename Actor> struct global_discovery_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&endpoint(const tcp::endpoint &value) &&noexcept {
        parent_t::config.endpoint = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&announce_url(const utils::URI &value) &&noexcept {
        parent_t::config.announce_url = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cert_file(const std::string &value) &&noexcept {
        parent_t::config.cert_file = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&key_file(const std::string &value) &&noexcept {
        parent_t::config.key_file = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&rx_buff_size(const std::uint32_t value) &&noexcept {
        parent_t::config.rx_buff_size = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct global_discovery_actor_t : public r::actor_base_t {
    using config_t = global_discovery_actor_config_t;
    template <typename Actor> using config_builder_t = global_discovery_actor_config_builder_t<Actor>;

    explicit global_discovery_actor_t(config_t &cfg);

    void on_start() noexcept override;
    // void shutdown_start() noexcept override;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_announce(message::http_response_t &message) noexcept;

  private:
    using rx_buff_t = payload::http_request_t::rx_buff_ptr_t;

    r::address_ptr_t http_client;
    tcp::endpoint endpoint;
    utils::URI announce_url;
    ssl_context_ptr_t ssl_context;
    rx_buff_t rx_buff;
    std::uint32_t rx_buff_size;
};

#if 0
namespace ssl = boost::asio::ssl;
using timer_t = asio::deadline_timer;

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
#endif

} // namespace net
} // namespace syncspirit
