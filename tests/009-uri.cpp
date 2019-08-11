#include "catch.hpp"
#include "utils/uri.h"

using namespace syncspirit::utils;

TEST_CASE("parse IGD control url", "[support]") {
    auto uri = parse("http://192.168.100.1:49652/upnp/control/WANIPConn1");
    REQUIRE((bool)uri);
    REQUIRE(uri->port == 49652);
    REQUIRE(uri->service == "49652");
    REQUIRE(uri->host == "192.168.100.1");
    REQUIRE(uri->proto == "http");
    REQUIRE(uri->path == "/upnp/control/WANIPConn1");
}
