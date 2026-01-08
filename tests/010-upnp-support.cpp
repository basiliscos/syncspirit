// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "test-utils.h"
#include "proto/upnp_support.h"

namespace sys = boost::system;
namespace bfs = std::filesystem;

using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("ssdp reply-1", "[support]") {
    auto body = read_file(locate_path("data/ssdp-reply-01.bin"));
    auto r = parse(body.data(), body.size());
    REQUIRE(r);

    auto &d = r.value();
    CHECK(d.location->c_str() == std::string_view("http://192.168.100.1:49652/49652gatedesc.xml"));
    CHECK(d.search_target == "urn:schemas-upnp-org:device:InternetGatewayDevice:1");
    CHECK(d.usn == "uuid:00e0fc37-2525-2828-2500-E8681986854D::urn:schemas-upnp-org:device:InternetGatewayDevice:1");
}

TEST_CASE("ssdp reply-2", "[support]") {
    auto body = read_file(locate_path("data/ssdp-reply-02.bin"));
    auto r = parse(body.data(), body.size());
    REQUIRE(r);

    auto &d = r.value();
    CHECK(d.location->c_str() == std::string_view("http://192.168.1.101:80/description.xml"));
    CHECK(d.search_target == "upnp:rootdevice");
    CHECK(d.usn == "uuid:2f402f80-da50-11e1-9b23-ecb5fa92d2ef::upnp:rootdevice");
}

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

int _init() {
    init_logging();
    return 1;
}

static int v = _init();
