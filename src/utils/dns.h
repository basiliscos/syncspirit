// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include <ares.h>
#include <vector>
#include <string>
#include <boost/asio.hpp>
#include "syncspirit-export.h"

namespace syncspirit::utils {

struct SYNCSPIRIT_API endpoint_t {
    boost::asio::ip::address host;
    std::string port;
    bool operator==(const endpoint_t &other) const noexcept { return host == other.host && port == other.port; }
};

using endpoints_t = std::vector<endpoint_t>;

}

namespace std {
using endpoint_t = syncspirit::utils::endpoint_t;

template <> struct hash<endpoint_t> {
    inline size_t operator()(const endpoint_t &endpoint) const noexcept {
        auto h1 = std::hash<boost::asio::ip::address>()(endpoint.host);
        auto h2 = std::hash<std::string>()(endpoint.port);
        return h1 ^ (h2 << 4);
    }
};

}

namespace syncspirit::utils {

SYNCSPIRIT_API std::vector<endpoint_t> parse_dns_servers(std::string_view str) noexcept;

}
