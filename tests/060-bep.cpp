#include "catch.hpp"
#include "test-utils.h"
#include "proto/bep_support.h"
#include "model/device_id.h"
#include "utils/uri.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::proto;
using namespace syncspirit::model;

TEST_CASE("announce", "[bep]") {
    unsigned char buff_raw[] = {
            0x2e, 0xa7, 0xd9, 0x0b, 0x0a, 0x20, 0x51, 0xe0, 0xd7, 0x6a, 0x5d, 0x84,
            0x90, 0xb8, 0xe2, 0xfc, 0x4e, 0x73, 0xcf, 0xaf, 0xff, 0x03, 0xe0, 0xda,
            0x9b, 0x29, 0x99, 0x15, 0x4f, 0xa0, 0xaf, 0x69, 0x1e, 0x16, 0x63, 0x8c,
            0xb9, 0x59, 0x12, 0x13, 0x74, 0x63, 0x70, 0x3a, 0x2f, 0x2f, 0x30, 0x2e,
            0x30, 0x2e, 0x30, 0x2e, 0x30, 0x3a, 0x32, 0x32, 0x30, 0x30, 0x30, 0x12,
            0x19, 0x74, 0x63, 0x70, 0x3a, 0x2f, 0x2f, 0x31, 0x39, 0x32, 0x2e, 0x31,
            0x36, 0x38, 0x2e, 0x31, 0x30, 0x30, 0x2e, 0x36, 0x3a, 0x32, 0x32, 0x30,
            0x30, 0x30, 0x12, 0x0f, 0x74, 0x63, 0x70, 0x3a, 0x2f, 0x2f, 0x30, 0x2e,
            0x30, 0x2e, 0x30, 0x2e, 0x30, 0x3a, 0x30, 0x12, 0x14, 0x71, 0x75, 0x69,
            0x63, 0x3a, 0x2f, 0x2f, 0x30, 0x2e, 0x30, 0x2e, 0x30, 0x2e, 0x30, 0x3a,
            0x32, 0x32, 0x30, 0x30, 0x30, 0x12, 0x1a, 0x71, 0x75, 0x69, 0x63, 0x3a,
            0x2f, 0x2f, 0x31, 0x39, 0x32, 0x2e, 0x31, 0x36, 0x38, 0x2e, 0x31, 0x30,
            0x30, 0x2e, 0x36, 0x3a, 0x32, 0x32, 0x30, 0x30, 0x30, 0x18, 0xaa, 0x97,
            0xfb, 0xd6, 0xb7, 0x99, 0xde, 0xd8, 0x40
    };

    constexpr auto buff_sz = sizeof(buff_raw);
    auto buff = boost::asio::buffer(buff_raw, buff_sz);
    auto r = parse_announce(buff);
    REQUIRE((bool)r);
    auto& v = r.value();
    auto device_id = device_id_t::from_sha256(v->id());
    REQUIRE((bool)device_id);
    CHECK(device_id.value().get_value() == "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD");
    REQUIRE(v->addresses_size() == 5);
    CHECK(v->addresses(1) == "tcp://192.168.100.6:22000");

    SECTION("make") {
        fmt::memory_buffer out;
        out.resize(1500);
        payload::URIs uris{utils::parse("tcp://192.168.100.6:22000").value()};
        auto sz = make_announce_message(out, "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD", uris, 1234);
        CHECK(sz == 99);
    }
}

TEST_CASE("hello", "[bep]") {
    unsigned char buff_raw[] = {
        0x2e, 0xa7, 0xd9, 0x0b, 0x00, 0x1f, 0x0a, 0x09,
        0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x68, 0x6f, 0x73,
        0x74, 0x12, 0x09, 0x73, 0x79, 0x6e, 0x63, 0x74,
        0x68, 0x69, 0x6e, 0x67, 0x1a, 0x07, 0x76, 0x31,
        0x2e, 0x31, 0x31, 0x2e, 0x31
    };

    constexpr auto buff_sz = sizeof(buff_raw);
    auto buff = boost::asio::buffer(buff_raw, buff_sz);
    auto r = parse_bep(buff);
    REQUIRE((bool)r);
    auto& v = r.value();
    CHECK(v.consumed == buff_sz);
    auto& msg = std::get<proto::message::Hello>(v.message);
    CHECK(msg->device_name() == "localhost");
    CHECK(msg->client_name() == "syncthing");
    CHECK(msg->client_version() == "v1.11.1");

    SECTION("make") {
        fmt::memory_buffer out;
        out.resize(1500);
        payload::URIs uris{utils::parse("tcp://192.168.100.6:22000").value()};
        make_hello_message(out, "test-device");
        auto buff = boost::asio::buffer(out.data(), out.size());
        auto r = parse_bep(buff);

        REQUIRE((bool)r);
        auto& v = r.value();
        CHECK(v.consumed == out.size());
        auto& msg = std::get<proto::message::Hello>(v.message);
        CHECK(msg->device_name() == "test-device");
        CHECK(msg->client_name() == "syncspirit");
        CHECK(msg->client_version() == "v0.01");
    }

}

TEST_CASE("cluster config", "[bep]") {
    unsigned char buff_raw[] = {
        0x00, 0x02, 0x10, 0x01, 0x00, 0x00, 0x00, 0xb5, 0x00, 0x00, 0x00, 0xc2,
        0xa1, 0x0a, 0xbf, 0x01, 0x0a, 0x04, 0x64, 0x61, 0x74, 0x61, 0x12, 0x06,
        0x00, 0xf5, 0x58, 0x82, 0x01, 0x34, 0x0a, 0x20, 0x45, 0x72, 0x75, 0x36,
        0xab, 0xf5, 0xe5, 0xb7, 0xf9, 0xe7, 0x8d, 0xbd, 0xbf, 0xeb, 0x54, 0x72,
        0xdd, 0x65, 0x06, 0x27, 0x18, 0xc6, 0x0b, 0x75, 0x04, 0xbe, 0xdc, 0x1f,
        0x14, 0xa4, 0x01, 0xbf, 0x12, 0x07, 0x68, 0x70, 0x2d, 0x6e, 0x6f, 0x74,
        0x65, 0x1a, 0x07, 0x64, 0x79, 0x6e, 0x61, 0x6d, 0x69, 0x63, 0x82, 0x01,
        0x42, 0x0a, 0x20, 0x51, 0xe0, 0xd7, 0x6a, 0x5d, 0x84, 0x90, 0xb8, 0xe2,
        0xfc, 0x4e, 0x73, 0xcf, 0xaf, 0xff, 0x03, 0xe0, 0xda, 0x9b, 0x29, 0x99,
        0x15, 0x4f, 0xa0, 0xaf, 0x69, 0x1e, 0x16, 0x63, 0x8c, 0xb9, 0x59, 0x12,
        0x09, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x68, 0x6f, 0x73, 0x74, 0x39, 0x00,
        0xc1, 0x30, 0x05, 0x40, 0xc3, 0xe4, 0xdd, 0xa2, 0xbe, 0x8a, 0xb2, 0xed,
        0x3d, 0x7c, 0x00, 0xf9, 0x11, 0x89, 0x7f, 0x93, 0xb0, 0x95, 0x15, 0x3f,
        0xd3, 0x52, 0xe4, 0xf5, 0xfa, 0xbd, 0x8c, 0xc5, 0x2d, 0x7a, 0x2d, 0x21,
        0x60, 0xf3, 0x76, 0x6e, 0xd7, 0xc5, 0xce, 0x27, 0x5f, 0xed, 0xe5, 0x76,
        0x12, 0x7c, 0x00, 0x50, 0x6e, 0x61, 0x6d, 0x69, 0x63
    };

    constexpr auto buff_sz = sizeof(buff_raw);
    auto buff = boost::asio::buffer(buff_raw, buff_sz);
    auto r = parse_bep(buff);
    REQUIRE((bool)r);
    auto& v = r.value();
    CHECK(v.consumed == buff_sz);
    auto& msg = std::get<proto::message::ClusterConfig>(v.message);
    /*
    CHECK(msg->device_name() == "localhost");
    CHECK(msg->client_name() == "syncthing");
    CHECK(msg->client_version() == "v1.11.1");
    */
}
