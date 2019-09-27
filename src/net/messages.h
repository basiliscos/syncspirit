#pragma once

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <rotor/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <memory>

#include <fmt/format.h>
#include "../utils/uri.h"
#include "../utils/upnp_support.h"

namespace syncspirit {
namespace net {

namespace r = rotor;
namespace ra = rotor::asio;
namespace asio = boost::asio;
namespace pt = boost::posix_time;
namespace http = boost::beast::http;
namespace sys = boost::system;

using tcp = asio::ip::tcp;
using udp = asio::ip::udp;
using v4 = asio::ip::address_v4;
using udp_socket_t = udp::socket;
using tcp_socket_t = tcp::socket;

/* outdated start */

struct listen_request_t {
    r::address_ptr_t reply_to;
    r::address_ptr_t redirect_to; /* address where redirect accepted peers (clients) */
    asio::ip::address address;
    std::uint16_t port;
};

struct listen_failure_t {
    sys::error_code ec;
};

struct listen_response_t {
    tcp::endpoint listening_endpoint;
};

struct new_peer_t {
    tcp::socket sock;
};

/* outdated end */

namespace payload {

struct http_response_t : public r::arc_base_t<http_response_t> {
    using raw_http_response_t = http::response<http::string_body>;
    tcp::endpoint local_endpoint;
    raw_http_response_t response;
    std::size_t bytes;

    http_response_t(const tcp::endpoint &endpoint_, raw_http_response_t &&response_, std::size_t bytes_)
        : local_endpoint(endpoint_), response(std::move(response_)), bytes{bytes_} {}
};

struct http_request_t {
    using rx_buff_t = boost::beast::flat_buffer;
    using rx_buff_ptr_t = std::shared_ptr<rx_buff_t>;
    using duration_t = r::pt::time_duration;
    using response_t = r::intrusive_ptr_t<http_response_t>;

    utils::URI url;
    fmt::memory_buffer data;
    rx_buff_ptr_t rx_buff;
    std::size_t rx_buff_size;
};

struct ssdp_response_t : r::arc_base_t<ssdp_response_t> {
    utils::discovery_result igd;
    ssdp_response_t(utils::discovery_result igd_) : igd{std::move(igd_)} {}
};

struct ssdp_request_t {
    using response_t = r::intrusive_ptr_t<ssdp_response_t>;
};

} // end of namespace payload

namespace message {

using http_request_t = r::request_traits_t<payload::http_request_t>::request::message_t;
using http_response_t = r::request_traits_t<payload::http_request_t>::response::message_t;

using ssdp_request_t = r::request_traits_t<payload::ssdp_request_t>::request::message_t;
using ssdp_response_t = r::request_traits_t<payload::ssdp_request_t>::response::message_t;

} // end of namespace message

} // namespace net
} // namespace syncspirit
