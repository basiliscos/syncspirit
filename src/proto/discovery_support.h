// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once
#include "utils/uri.h"
#include "model/device_id.h"
#include "syncspirit-export.h"
#include "utils/bytes.h"
#include <boost/outcome.hpp>
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

namespace syncspirit::proto {

namespace outcome = boost::outcome_v2;
namespace asio = boost::asio;
namespace http = boost::beast::http;

SYNCSPIRIT_API outcome::result<utils::uri_ptr_t>
make_announce_request(utils::bytes_t &buff, const utils::uri_ptr_t &announce_uri,
                      const utils::uri_container_t &listening_uris) noexcept;

SYNCSPIRIT_API outcome::result<utils::uri_ptr_t> make_discovery_request(utils::bytes_t &buff,
                                                                        const utils::uri_ptr_t &announce_uri,
                                                                        const model::device_id_t device_id) noexcept;

SYNCSPIRIT_API outcome::result<std::uint32_t> parse_announce(http::response<http::string_body> &res) noexcept;

SYNCSPIRIT_API outcome::result<utils::uri_container_t> parse_contact(http::response<http::string_body> &res) noexcept;

} // namespace syncspirit::proto
