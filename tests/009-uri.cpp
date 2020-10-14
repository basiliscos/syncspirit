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

    std::string relay = "relay://188.68.32.45:22067/?id=O4LHPKG-O6BQ36W-MUOVKTI-MKAVHSC-Y7EC3U4-DHNLEDE-MZBJWQN-UIX6QAL\u0026pingInterval=1m0s\u0026networkTimeout=2m0s\u0026sessionLimitBps=0\u0026globalLimitBps=0\u0026statusAddr=:22070\u0026providedBy=🐾 TilCreator (tils.pw)";
    REQUIRE((bool)parse(relay) == false);

}
