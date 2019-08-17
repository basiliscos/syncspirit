#pragma once

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <rotor/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <fmt/format.h>
#include "../utils/uri.h"
#include "../utils/upnp_support.h"

namespace syncspirit {
namespace net {

namespace asio = boost::asio;
namespace pt = boost::posix_time;
namespace r = rotor;
namespace http = boost::beast::http;
namespace sys = boost::system;
using tcp = asio::ip::tcp;

struct request_t {
    utils::URI url;
    fmt::memory_buffer data;
    pt::milliseconds timeout;
    r::address_ptr_t reply_to;
    std::size_t rx_buff_size;
};

struct response_t {
    using http_response_t = http::response<http::string_body>;
    tcp::endpoint local_endpoint;
    utils::URI url;
    boost::beast::flat_buffer data;
    http_response_t response;
    std::size_t bytes;
};

struct request_failed_t {
    sys::error_code ec;
    utils::URI url;
};

struct ssdp_failed_t {
    sys::error_code ec;
};

using ssdp_result_t = utils::discovery_result;

} // namespace net
} // namespace syncspirit
