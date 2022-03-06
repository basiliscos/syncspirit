// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once
#include "../utils/uri.h"
#include "../model/device_id.h"
#include <fmt/fmt.h>
#include <string>
#include <vector>
#include <boost/outcome.hpp>
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

namespace syncspirit::proto {

namespace outcome = boost::outcome_v2;
namespace asio = boost::asio;
namespace http = boost::beast::http;

outcome::result<utils::URI> make_announce_request(fmt::memory_buffer &buff, const utils::URI &announce_uri,
                                                  const utils::uri_container_t &listening_uris) noexcept;
outcome::result<utils::URI> make_discovery_request(fmt::memory_buffer &buff, const utils::URI &announce_uri,
                                                   const model::device_id_t device_id) noexcept;

outcome::result<std::uint32_t> parse_announce(http::response<http::string_body> &res) noexcept;

outcome::result<utils::uri_container_t> parse_contact(http::response<http::string_body> &res) noexcept;

} // namespace syncspirit::proto
