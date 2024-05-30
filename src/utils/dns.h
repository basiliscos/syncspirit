// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include <ares.h>
#include <vector>
#include <string>
#include <cstdint>
#include <boost/asio.hpp>
#include "syncspirit-export.h"

namespace syncspirit::utils {

struct SYNCSPIRIT_API endpoint_t {
    boost::asio::ip::address ip;
    std::uint16_t port;
    bool operator==(const endpoint_t &other) const noexcept { return ip == other.ip && port == other.port; }
};

struct SYNCSPIRIT_API dns_query_t {
    std::string host;
    std::uint16_t port;

    bool operator==(const dns_query_t &other) const noexcept = default;
};

using endpoints_t = std::vector<endpoint_t>;

} // namespace syncspirit::utils

namespace std {
using dns_query_t = syncspirit::utils::dns_query_t;

template <> struct hash<dns_query_t> {
    inline size_t operator()(const dns_query_t &query) const noexcept {
        auto h1 = std::hash<std::string>()(query.host);
        auto h2 = std::hash<std::uint16_t>()(query.port);
        return h1 ^ (h2 << 4);
    }
};

} // namespace std

namespace syncspirit::utils {

SYNCSPIRIT_API std::vector<endpoint_t> parse_dns_servers(std::string_view str) noexcept;

}
