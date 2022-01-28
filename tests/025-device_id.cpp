#include "catch.hpp"
#include "test-utils.h"
#include "model/device_id.h"
#include "proto/luhn32.h"
#include "utils/tls.h"

using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::model;
using namespace syncspirit::proto;

namespace fs = boost::filesystem;
using l32 = luhn32;

TEST_CASE("luhn32", "[model]") {
    REQUIRE(l32::calculate("WG2IWWALPC2HZ") == 'H');
    REQUIRE(l32::calculate("KHQNO2S5QSILR") == 'K');
    REQUIRE(l32::validate("KHQNO2S5QSILRK"));
    REQUIRE(!l32::validate("KHQNO2S5QSILR"));
    REQUIRE(l32::validate("WG2IWWALPC2HZH"));
    REQUIRE(!l32::validate("WG2IWWALPC2HZ"));
    REQUIRE(l32::validate("MFZWI3DBONSGYC"));
    REQUIRE(!l32::validate("MFZWI3DBONSGY"));
}

TEST_CASE("device_id", "[model]") {
    auto cert_path = file_path("/data/sample-cert.pem");
    auto key_path = file_path("/data/sample-key.pem");
    auto load_result = load_pair(cert_path.c_str(), key_path.c_str());
    REQUIRE(load_result);
    auto &pair = load_result.value();
    auto opt_device_id = device_id_t::from_cert(pair.cert_data);
    REQUIRE(opt_device_id);

    auto &device_id = opt_device_id.value();
    auto expected = std::string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD");
    REQUIRE(device_id.get_value().size() == expected.size());
    REQUIRE(device_id.get_value() == expected);
    CHECK(device_id.get_short() == "KHQNO2S");

    auto opt = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD");
    REQUIRE((bool)opt);
    CHECK(opt.value() == device_id);

    opt = device_id_t::from_string("KHQNO2S-5QSILRE-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD");
    REQUIRE(!opt);

    opt = device_id_t::from_sha256(device_id.get_sha256());
    REQUIRE((bool)opt);
    CHECK(opt.value() == device_id);

    CHECK(opt.value() != local_device_id);
}
