#pragma once
#include "../utils/uri.h"
#include "../model/device_id.h"
#include <fmt/format.h>
#include <string>
#include <vector>
#include <boost/outcome.hpp>
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

namespace syncspirit::proto {

namespace outcome = boost::outcome_v2;
namespace asio = boost::asio;
namespace http = boost::beast::http;
using tcp = asio::ip::tcp;

outcome::result<utils::URI> make_announce_request(fmt::memory_buffer &buff, const utils::URI &announce_uri,
                                                  const tcp::endpoint &endpoint) noexcept;
outcome::result<utils::URI> make_discovery_request(fmt::memory_buffer &buff, const utils::URI &announce_uri,
                                                   const model::device_id_t device_id) noexcept;

outcome::result<std::uint32_t> parse_announce(http::response<http::string_body> &res) noexcept;

} // namespace syncspirit::proto
