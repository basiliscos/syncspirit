#include "catch.hpp"
#include "test-utils.h"
#include "proto/discovery_support.h"
#include <boost/filesystem.hpp>
#include <iostream>

namespace sys = boost::system;
namespace fs = boost::filesystem;

using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("parse IGD description", "[support]") {
    std::string body = R""(
                       {"seen":"2020-10-13T18:41:37.02287354Z",
                       "addresses":["quic://192.168.100.15:22000",
                       "quic://81.52.93.44:22000",
                       "quic://81.52.93.44:22000",
                       "relay://188.68.32.45:22067/?id=O4LHPKG-O6BQ36W-MUOVKTI-MKAVHSC-Y7EC3U4-DHNLEDE-MZBJWQN-UIX6QAL\u0026pingInterval=1m0s\u0026networkTimeout=2m0s\u0026sessionLimitBps=0\u0026globalLimitBps=0\u0026statusAddr=:22070\u0026providedBy=🐾 TilCreator (tils.pw)",
                       "tcp://192.168.100.15:22000",
                       "tcp://81.52.93.44:22000",
                       "tcp://81.52.93.44:22000",
                       "tcp://81.52.93.44:55386",
                       "tcp://81.52.93.44:55386"]})"";

    http::response<http::string_body> res;
    res.result(200);
    res.body() = body;
    auto r =  parse_contact(res);
    CHECK((bool)r);
}
