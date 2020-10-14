#pragma once
#include "uri.h"
#include "../proto/device_id.h"
#include <fmt/format.h>
#include <string>
#include <vector>
#include <boost/outcome.hpp>
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

namespace syncspirit::utils {

namespace outcome = boost::outcome_v2;
namespace asio = boost::asio;
namespace http = boost::beast::http;
using tcp = asio::ip::tcp;

outcome::result<URI> make_announce_request(fmt::memory_buffer &buff, const URI &announce_uri,
                                           const tcp::endpoint &endpoint) noexcept;
outcome::result<URI> make_discovery_request(fmt::memory_buffer &buff, const URI &announce_uri,
                                            const proto::device_id_t device_id) noexcept;

outcome::result<std::uint32_t> parse_announce(http::response<http::string_body> &res) noexcept;

} // namespace syncspirit::utils
