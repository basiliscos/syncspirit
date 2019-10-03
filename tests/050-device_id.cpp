#include "catch.hpp"
#include "test-utils.h"
#include "proto/luhn32.h"

using namespace syncspirit::test;

namespace fs = boost::filesystem;
using l32 = syncspirit::proto::luhn32;

TEST_CASE("luhn32", "[protocol]") {
    REQUIRE(l32::calculate("WG2IWWALPC2HZ") == 'H');
    REQUIRE(l32::calculate("KHQNO2S5QSILR") == 'K');
    REQUIRE(l32::validate("KHQNO2S5QSILRK"));
    REQUIRE(!l32::validate("KHQNO2S5QSILR"));
    REQUIRE(l32::validate("WG2IWWALPC2HZH"));
    REQUIRE(!l32::validate("WG2IWWALPC2HZ"));
    REQUIRE(l32::validate("MFZWI3DBONSGYC"));
    REQUIRE(!l32::validate("MFZWI3DBONSGY"));
}
