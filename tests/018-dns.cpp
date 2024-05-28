// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "utils/dns.h"

using namespace syncspirit::utils;
namespace ip = boost::asio::ip;

TEST_CASE("get_block_size", "[support]") {
    auto str = "192.168.1.100,192.168.1.101:53,[1:2:3::4]:53,[fe80::1]:53%eth0";
    auto endpoints = parse_dns_servers(str);
    CHECK(endpoints.size() == 4u);

    auto &e0 = endpoints[0];
    CHECK(e0.host == ip::make_address_v4("192.168.1.100"));
    CHECK(e0.port == "53");

    auto &e1 = endpoints[1];
    CHECK(e1.host == ip::make_address_v4("192.168.1.101"));
    CHECK(e1.port == "53");

    auto &e2 = endpoints[2];
    CHECK(e2.host == ip::make_address_v6("1:2:3::4"));
    CHECK(e2.port == "53");

    auto &e3 = endpoints[3];
    CHECK(e3.host == ip::make_address_v6("fe80::1"));
    CHECK(e3.port == "53");
}
