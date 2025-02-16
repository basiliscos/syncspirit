// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <boost/asio.hpp>
#include <memory>
#include "syncspirit-export.h"

namespace syncspirit::utils {

namespace ip = boost::asio::ip;

struct SYNCSPIRIT_API endpoint_t {
    ip::address ip;
    std::uint16_t port;
    bool operator==(const endpoint_t &other) const noexcept { return ip == other.ip && port == other.port; }
};

struct SYNCSPIRIT_API dns_query_t {
    std::string host;
    bool operator==(const dns_query_t &other) const noexcept = default;
};

using endpoints_t = std::vector<endpoint_t>;

template <typename Endpoint> using addresses_t = std::shared_ptr<std::vector<Endpoint>>;

template <typename Protocol, typename Endpoint = typename Protocol::endpoint>
addresses_t<Endpoint> make_endpoints(const std::vector<ip::address> &addresses, std::uint16_t port) noexcept;

} // namespace syncspirit::utils

namespace std {
using dns_query_t = syncspirit::utils::dns_query_t;

template <> struct hash<dns_query_t> {
    inline size_t operator()(const dns_query_t &query) const noexcept { return std::hash<std::string>()(query.host); }
};

} // namespace std

namespace syncspirit::utils {

SYNCSPIRIT_API std::vector<endpoint_t> parse_dns_servers(std::string_view str) noexcept;

std::string_view SYNCSPIRIT_API cares_version() noexcept;

} // namespace syncspirit::utils
