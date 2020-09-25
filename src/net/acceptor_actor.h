#pragma once

#include "../configuration.h"
#include "../utils/upnp_support.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <rotor/asio/supervisor_asio.h>

namespace syncspirit {
namespace net {

struct acceptor_actor_config_t : public r::actor_config_t {
    asio::ip::address local_address;
};

template <typename Actor> struct acceptor_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&local_address(const asio::ip::address &value) &&noexcept {
        parent_t::config.local_address = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};


struct acceptor_actor_t : public r::actor_base_t {
    using config_t = acceptor_actor_config_t;
    template <typename Actor> using config_builder_t = acceptor_actor_config_builder_t<Actor>;

    acceptor_actor_t(config_t& config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_start() noexcept override;
    void on_start() noexcept override;

private:
    void on_endpoint_request(message::endpoint_request_t&) noexcept;
    using tcp_socket_option_t = boost::optional<tcp_socket_t>;

    void accept_next() noexcept;
    void on_accept(const sys::error_code &ec) noexcept;

    asio::io_context::strand &strand;
    tcp_socket_t sock;
    tcp::endpoint endpoint;
    tcp::acceptor acceptor;
    tcp_socket_t peer;
    //r::address_ptr_t redirect_to;
};

} // namespace net
} // namespace syncspirit
