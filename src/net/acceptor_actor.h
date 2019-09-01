#pragma once

#include "../configuration.h"
#include "../utils/upnp_support.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <rotor/asio/supervisor_asio.h>

namespace syncspirit {
namespace net {

struct acceptor_actor_t : public r::actor_base_t {
  public:
    acceptor_actor_t(ra::supervisor_asio_t &sup);
    virtual void on_initialize(r::message::init_request_t &) noexcept override;
    virtual void on_shutdown(r::message::shutdown_request_t &) noexcept override;
    virtual void on_listen_request(r::message_t<listen_request_t> &) noexcept;

  private:
    // using tcp_socket_option_t = boost::optional<tcp_socket_t>;
    // using endpoint_t = tcp::endpoint;

    void accept_next() noexcept;
    void on_accept(const sys::error_code &ec) noexcept;

    asio::io_context::strand &strand;
    asio::io_context &io_context;
    tcp::acceptor acceptor;
    tcp_socket_t peer;
    r::address_ptr_t redirect_to;
    bool accepting;
};

} // namespace net
} // namespace syncspirit
