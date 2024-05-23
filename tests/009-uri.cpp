// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "utils/uri.h"

using namespace syncspirit::utils;

TEST_CASE("parse IGD control url", "[support]") {
    auto uri = parse("http://192.168.100.1:49652/upnp/control/WANIPConn1");
    REQUIRE((bool)uri);
    CHECK(uri->port_number() == 49652);
    CHECK(uri->port() == "49652");
    CHECK(uri->host() == "192.168.100.1");
    CHECK(uri->scheme() == "http");
    CHECK(uri->encoded_path() == "/upnp/control/WANIPConn1");

    std::string relay = "relay://188.68.32.45:22067/"
                        "?id=O4LHPKG-O6BQ36W-MUOVKTI-MKAVHSC-Y7EC3U4-DHNLEDE-MZBJWQN-UIX6QAL\u0026pingInterval="
                        "1m0s\u0026networkTimeout=2m0s\u0026sessionLimitBps=0\u0026globalLimitBps=0\u0026statusAddr=:"
                        "22070\u0026providedBy=🐾 TilCreator (tils.pw)";
    REQUIRE((bool)parse(relay) == false);

    std::string relay2 = "relay://188.68.32.45:22067/"
                         "?id=O4LHPKG-O6BQ36W-MUOVKTI-MKAVHSC-Y7EC3U4-DHNLEDE-MZBJWQN-UIX6QAL&pingInterval=1m0s&"
                         "networkTimeout=2m0s&sessionLimitBps=0&globalLimitBps=0&statusAddr=22070";
    REQUIRE((bool)parse(relay2) == true);
}
