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

struct request_t {
    using rx_buff_t = boost::beast::flat_buffer;
    using rx_buff_ptr_t = std::shared_ptr<rx_buff_t>;
    utils::URI url;
    fmt::memory_buffer data;
    pt::milliseconds timeout;
    r::address_ptr_t reply_to;
    rx_buff_ptr_t rx_buff;
    std::size_t rx_buff_size;
};

struct response_t {
    using http_response_t = http::response<http::string_body>;
    request_t::rx_buff_ptr_t rx_buff;
    tcp::endpoint local_endpoint;
    utils::URI url;
    http_response_t response;
    std::size_t bytes;
};

struct request_failed_t {
    sys::error_code ec;
    utils::URI url;
};

struct ssdp_failure_t {
    sys::error_code ec;
};

struct try_again_request_t {};

using ssdp_result_t = utils::discovery_result;

} // namespace net
} // namespace syncspirit
