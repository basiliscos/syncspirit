// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "catch.hpp"
#include "test-utils.h"
#include "proto/upnp_support.h"
#include <boost/filesystem.hpp>
#include <iostream>

namespace sys = boost::system;
namespace bfs = boost::filesystem;

using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("parse IGD description", "[support]") {
    auto xml = read_file(locate_path("data/49652gatedesc.xml"));
    auto wan_service = parse_igd(xml.c_str(), xml.size());
    REQUIRE(wan_service);
    REQUIRE(wan_service.value().control_path == "/upnp/control/WANIPConn1");
    REQUIRE(wan_service.value().description_path == "/gateconnSCPD.xml");
}

TEST_CASE("parse external IP", "[support]") {
    auto xml = read_file(locate_path("data/external-ip.xml"));
    auto ip = parse_external_ip(xml.c_str(), xml.size());
    REQUIRE(ip);
    REQUIRE(ip.value() == "81.31.113.9");
}

TEST_CASE("parse successful port mapping", "[support]") {
    auto xml = read_file(locate_path("data/port-mapping-success.xml"));
    auto r = parse_mapping(xml.c_str(), xml.size());
    REQUIRE(r);
    REQUIRE(r.value() == true);
}

TEST_CASE("parse failed port mapping", "[support]") {
    auto xml = read_file(locate_path("data/soap-failure.xml"));
    auto r = parse_mapping(xml.c_str(), xml.size());
    REQUIRE(r);
    REQUIRE(r.value() == false);
}

TEST_CASE("parse failed port unmapping", "[support]") {
    auto xml = read_file(locate_path("data/port-unmapping-failure.xml"));
    auto r = parse_unmapping(xml.c_str(), xml.size());
    REQUIRE(r);
    REQUIRE(r.value() == false);
}
