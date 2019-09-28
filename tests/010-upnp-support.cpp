#include "catch.hpp"
#include "test-utils.h"
#include "utils/upnp_support.h"
#include <boost/filesystem.hpp>
#include <iostream>

namespace sys = boost::system;
namespace fs = boost::filesystem;

using namespace syncspirit::utils;
using namespace syncspirit::test;

TEST_CASE("parse IGD description", "[support]") {
    auto xml = read_file("/tests/data/49652gatedesc.xml");
    auto wan_service = parse_igd(xml.c_str(), xml.size());
    REQUIRE(wan_service);
    REQUIRE(wan_service.value().control_path == "/upnp/control/WANIPConn1");
    REQUIRE(wan_service.value().description_path == "/gateconnSCPD.xml");
}

TEST_CASE("parse external IP", "[support]") {
    auto xml = read_file("/tests/data/external-ip.xml");
    auto ip = parse_external_ip(xml.c_str(), xml.size());
    REQUIRE(ip);
    REQUIRE(ip.value() == "81.31.113.9");
}

TEST_CASE("parse successful port mapping", "[support]") {
    auto xml = read_file("/tests/data/port-mapping-success.xml");
    auto r = parse_mapping(xml.c_str(), xml.size());
    REQUIRE(r);
    REQUIRE(r.value() == true);
}

TEST_CASE("parse failede port mapping", "[support]") {
    auto xml = read_file("/tests/data/soap-failure.xml");
    auto r = parse_mapping(xml.c_str(), xml.size());
    REQUIRE(r);
    REQUIRE(r.value() == false);
}
