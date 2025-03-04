// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "utils/dns.h"

using namespace syncspirit::utils;
namespace ip = boost::asio::ip;

TEST_CASE("get_block_size", "[support]") {
    auto str = "192.168.1.100,192.168.1.101:53,[1:2:3::4]:53,[fe80::1]:53%eth0";
    auto endpoints = parse_dns_servers(str);
#if !defined(_WIN32_WINNT) || _WIN32_WINNT >= 0x0600
    CHECK(endpoints.size() == 4u);
#else
    CHECK(endpoints.size() == 2u);
#endif

    auto &e0 = endpoints[0];
    CHECK(e0.ip == ip::make_address_v4("192.168.1.100"));
    CHECK(e0.port == 53);

    auto &e1 = endpoints[1];
    CHECK(e1.ip == ip::make_address_v4("192.168.1.101"));
    CHECK(e1.port == 53);

#if !defined(_WIN32_WINNT) || _WIN32_WINNT >= 0x0600
    auto &e2 = endpoints[2];
    CHECK(e2.ip == ip::make_address_v6("1:2:3::4"));
    CHECK(e2.port == 53);

    auto &e3 = endpoints[3];
    CHECK(e3.ip == ip::make_address_v6("fe80::1"));
    CHECK(e3.port == 53);
#endif
}
