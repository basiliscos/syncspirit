#pragma once

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <rotor/asio.hpp>
#include <boost/asio/ssl.hpp>
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
namespace ssl = asio::ssl;

using tcp = asio::ip::tcp;
using udp = asio::ip::udp;
using v4 = asio::ip::address_v4;
using udp_socket_t = udp::socket;
using tcp_socket_t = tcp::socket;

extern r::pt::time_duration default_timeout;

using ssl_context_ptr_t = std::shared_ptr<ssl::context>;

namespace payload {

struct address_response_t : public r::arc_base_t<address_response_t> {
    using resolve_results_t = tcp::resolver::results_type;

    explicit address_response_t(resolve_results_t results_) : results{results_} {};
    resolve_results_t results;
};

struct address_request_t : public r::arc_base_t<address_request_t> {
    using response_t = r::intrusive_ptr_t<address_response_t>;
    std::string host;
    std::string port;
    address_request_t(const std::string &host_, std::string &port_) : host{host_}, port{port_} {}
};

struct endpoint_response_t {
    tcp::endpoint local_endpoint;
};

struct endpoint_request_t {
    using response_t = endpoint_response_t;
};

struct http_response_t : public r::arc_base_t<http_response_t> {
    using raw_http_response_t = http::response<http::string_body>;
    raw_http_response_t response;
    std::size_t bytes;

    http_response_t(raw_http_response_t &&response_, std::size_t bytes_)
        : response(std::move(response_)), bytes{bytes_} {}
};

struct http_request_t : r::arc_base_t<http_request_t> {
    using rx_buff_t = boost::beast::flat_buffer;
    using rx_buff_ptr_t = std::shared_ptr<rx_buff_t>;
    using duration_t = r::pt::time_duration;
    using response_t = r::intrusive_ptr_t<http_response_t>;

    utils::URI url;
    fmt::memory_buffer data;
    rx_buff_ptr_t rx_buff;
    std::size_t rx_buff_size;
    ssl_context_ptr_t ssl_context;

    http_request_t(utils::URI &url_, fmt::memory_buffer &&data_, rx_buff_ptr_t rx_buff_, std::size_t rx_buff_size_,
                   ssl_context_ptr_t ssl_context_ = {})
        : url{url_}, data{std::move(data_)}, rx_buff{rx_buff_}, rx_buff_size{rx_buff_size_}, ssl_context{std::move(
                                                                                                 ssl_context_)} {}
};

struct ssdp_notification_t : r::arc_base_t<ssdp_notification_t> {
    utils::discovery_result igd;
    asio::ip::address local_address;
    ssdp_notification_t(utils::discovery_result &&igd_, const asio::ip::address &local_address_)
        : igd{std::move(igd_)}, local_address{local_address_} {}
};

struct port_mapping_notification_t {
    asio::ip::address external_ip;
    bool success;
};

} // end of namespace payload

namespace message {

using ssdp_notification_t = r::message_t<payload::ssdp_notification_t>;
using port_mapping_notification_t = r::message_t<payload::port_mapping_notification_t>;

using resolve_request_t = r::request_traits_t<payload::address_request_t>::request::message_t;
using resolve_response_t = r::request_traits_t<payload::address_request_t>::response::message_t;

using http_request_t = r::request_traits_t<payload::http_request_t>::request::message_t;
using http_response_t = r::request_traits_t<payload::http_request_t>::response::message_t;

using endpoint_request_t = r::request_traits_t<payload::endpoint_request_t>::request::message_t;
using endpoint_response_t = r::request_traits_t<payload::endpoint_request_t>::response::message_t;

} // end of namespace message

} // namespace net
} // namespace syncspirit
