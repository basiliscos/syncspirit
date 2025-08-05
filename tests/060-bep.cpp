// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "proto/bep_support.h"
#include "model/device_id.h"
#include "utils/error_code.h"
#include "utils/uri.h"
#include "syncspirit-config.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::proto;
using namespace syncspirit::model;

TEST_CASE("announce", "[bep]") {
    unsigned char buff_raw[] = {
        0x2e, 0xa7, 0xd9, 0x0b, 0x0a, 0x20, 0x51, 0xe0, 0xd7, 0x6a, 0x5d, 0x84, 0x90, 0xb8, 0xe2, 0xfc, 0x4e,
        0x73, 0xcf, 0xaf, 0xff, 0x03, 0xe0, 0xda, 0x9b, 0x29, 0x99, 0x15, 0x4f, 0xa0, 0xaf, 0x69, 0x1e, 0x16,
        0x63, 0x8c, 0xb9, 0x59, 0x12, 0x13, 0x74, 0x63, 0x70, 0x3a, 0x2f, 0x2f, 0x30, 0x2e, 0x30, 0x2e, 0x30,
        0x2e, 0x30, 0x3a, 0x32, 0x32, 0x30, 0x30, 0x30, 0x12, 0x19, 0x74, 0x63, 0x70, 0x3a, 0x2f, 0x2f, 0x31,
        0x39, 0x32, 0x2e, 0x31, 0x36, 0x38, 0x2e, 0x31, 0x30, 0x30, 0x2e, 0x36, 0x3a, 0x32, 0x32, 0x30, 0x30,
        0x30, 0x12, 0x0f, 0x74, 0x63, 0x70, 0x3a, 0x2f, 0x2f, 0x30, 0x2e, 0x30, 0x2e, 0x30, 0x2e, 0x30, 0x3a,
        0x30, 0x12, 0x14, 0x71, 0x75, 0x69, 0x63, 0x3a, 0x2f, 0x2f, 0x30, 0x2e, 0x30, 0x2e, 0x30, 0x2e, 0x30,
        0x3a, 0x32, 0x32, 0x30, 0x30, 0x30, 0x12, 0x1a, 0x71, 0x75, 0x69, 0x63, 0x3a, 0x2f, 0x2f, 0x31, 0x39,
        0x32, 0x2e, 0x31, 0x36, 0x38, 0x2e, 0x31, 0x30, 0x30, 0x2e, 0x36, 0x3a, 0x32, 0x32, 0x30, 0x30, 0x30,
        0x18, 0xaa, 0x97, 0xfb, 0xd6, 0xb7, 0x99, 0xde, 0xd8, 0x40};

    constexpr auto buff_sz = sizeof(buff_raw);
    auto buff = utils::bytes_view_t(buff_raw, buff_sz);
    auto r = parse_announce(buff);
    REQUIRE((bool)r);
    auto &v = r.value();
    auto device_id = device_id_t::from_sha256(proto::get_id(v));
    REQUIRE((bool)device_id);
    CHECK(device_id.value().get_value() == "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD");
    REQUIRE(proto::get_addresses_size(v) == 5);
    CHECK(proto::get_addresses(v, 1) == "tcp://192.168.100.6:22000");

    SECTION("encode & decode") {
        auto bytes = utils::bytes_t(1500);
        auto out = utils::bytes_view_t(bytes);
        payload::URIs uris{utils::parse("tcp://192.168.100.6:22000")};
        auto device_id_bytes = device_id.value().get_sha256();
        auto sz = make_announce_message(out, device_id_bytes, uris, 1234);
        REQUIRE(sz > 50);

        auto in = utils::bytes_view_t(bytes.data(), sz);
        auto r_ = parse_announce(in);
        REQUIRE((bool)r_);
        auto &v_ = r_.value();
        auto device_id_ = device_id_t::from_sha256(proto::get_id(v_));
        REQUIRE(proto::get_addresses_size(v_) == 1);
        CHECK(proto::get_addresses(v_, 0) == "tcp://192.168.100.6:22000");
        CHECK(proto::get_instance_id(v_) == 1234);
    }
}

TEST_CASE("hello", "[bep]") {
    unsigned char buff_raw[] = {0x2e, 0xa7, 0xd9, 0x0b, 0x00, 0x1f, 0x0a, 0x09, 0x6c, 0x6f, 0x63, 0x61, 0x6c,
                                0x68, 0x6f, 0x73, 0x74, 0x12, 0x09, 0x73, 0x79, 0x6e, 0x63, 0x74, 0x68, 0x69,
                                0x6e, 0x67, 0x1a, 0x07, 0x76, 0x31, 0x2e, 0x31, 0x31, 0x2e, 0x31};

    constexpr auto buff_sz = sizeof(buff_raw);
    auto buff = utils::bytes_view_t(buff_raw, buff_sz);
    auto r = parse_bep(buff);
    REQUIRE((bool)r);
    auto &v = r.value();
    CHECK(v.consumed == buff_sz);
    auto &msg = std::get<proto::Hello>(v.message);
    CHECK(proto::get_device_name(msg) == "localhost");
    CHECK(proto::get_client_name(msg) == "syncthing");
    CHECK(proto::get_client_version(msg) == "v1.11.1");

    SECTION("make") {
        auto out = make_hello_message("test-device");
        auto buff = utils::bytes_view_t(out.data(), out.size());
        auto r = parse_bep(buff);

        REQUIRE((bool)r);
        auto &v = r.value();
        CHECK(v.consumed == out.size());
        auto &msg = std::get<proto::Hello>(v.message);
        CHECK(proto::get_device_name(msg) == "test-device");
        CHECK(proto::get_client_name(msg) == "syncspirit");
        CHECK(proto::get_client_version(msg) == SYNCSPIRIT_VERSION);
    }
}

TEST_CASE("cluster config", "[bep]") {
    unsigned char buff_raw[] = {
        0x00, 0x02, 0x10, 0x01, 0x00, 0x00, 0x00, 0xb5, 0x00, 0x00, 0x00, 0xc2, 0xa1, 0x0a, 0xbf, 0x01, 0x0a, 0x04,
        0x64, 0x61, 0x74, 0x61, 0x12, 0x06, 0x00, 0xf5, 0x58, 0x82, 0x01, 0x34, 0x0a, 0x20, 0x45, 0x72, 0x75, 0x36,
        0xab, 0xf5, 0xe5, 0xb7, 0xf9, 0xe7, 0x8d, 0xbd, 0xbf, 0xeb, 0x54, 0x72, 0xdd, 0x65, 0x06, 0x27, 0x18, 0xc6,
        0x0b, 0x75, 0x04, 0xbe, 0xdc, 0x1f, 0x14, 0xa4, 0x01, 0xbf, 0x12, 0x07, 0x68, 0x70, 0x2d, 0x6e, 0x6f, 0x74,
        0x65, 0x1a, 0x07, 0x64, 0x79, 0x6e, 0x61, 0x6d, 0x69, 0x63, 0x82, 0x01, 0x42, 0x0a, 0x20, 0x51, 0xe0, 0xd7,
        0x6a, 0x5d, 0x84, 0x90, 0xb8, 0xe2, 0xfc, 0x4e, 0x73, 0xcf, 0xaf, 0xff, 0x03, 0xe0, 0xda, 0x9b, 0x29, 0x99,
        0x15, 0x4f, 0xa0, 0xaf, 0x69, 0x1e, 0x16, 0x63, 0x8c, 0xb9, 0x59, 0x12, 0x09, 0x6c, 0x6f, 0x63, 0x61, 0x6c,
        0x68, 0x6f, 0x73, 0x74, 0x39, 0x00, 0xc1, 0x30, 0x05, 0x40, 0xc3, 0xe4, 0xdd, 0xa2, 0xbe, 0x8a, 0xb2, 0xed,
        0x3d, 0x7c, 0x00, 0xf9, 0x11, 0x89, 0x7f, 0x93, 0xb0, 0x95, 0x15, 0x3f, 0xd3, 0x52, 0xe4, 0xf5, 0xfa, 0xbd,
        0x8c, 0xc5, 0x2d, 0x7a, 0x2d, 0x21, 0x60, 0xf3, 0x76, 0x6e, 0xd7, 0xc5, 0xce, 0x27, 0x5f, 0xed, 0xe5, 0x76,
        0x12, 0x7c, 0x00, 0x50, 0x6e, 0x61, 0x6d, 0x69, 0x63};

    constexpr auto buff_sz = sizeof(buff_raw);
    auto buff = utils::bytes_view_t(buff_raw, buff_sz);
    auto r = parse_bep(buff);
    REQUIRE((bool)r);
    auto &v = r.value();
    CHECK(v.consumed == buff_sz);
    auto &msg = std::get<proto::ClusterConfig>(v.message);
    REQUIRE(proto::get_folders_size(msg) == 1);
    auto &folder = proto::get_folders(msg, 0);
    CHECK(proto::get_label(folder) == "data");
    CHECK(!proto::get_read_only(folder));
    REQUIRE(proto::get_devices_size(folder) == 3);
    auto &d1 = proto::get_devices(folder, 0);
    auto &d2 = proto::get_devices(folder, 1);
    auto &d3 = proto::get_devices(folder, 2);

    auto d1_id = device_id_t::from_sha256(proto::get_id(d1)).value().get_value();
    auto d2_id = device_id_t::from_sha256(proto::get_id(d2)).value().get_value();
    auto d3_id = device_id_t::from_sha256(proto::get_id(d3)).value().get_value();
    CHECK(d1_id == "IVZHKNV-L6XS3PS-6PHRW63-722UOLD-OWKBRHD-DDAW5IV-EX3OB6F-FEAG7QQ");
    CHECK(d2_id == "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD");
    CHECK(d3_id == "RF7ZHME-VCU75G2-UXE6X5L-3DGFFVV-5C2ILA6-N3G5V6F-FZYTV73-PFOYJAR");

    CHECK(!proto::get_introducer(d1));
    CHECK(!proto::get_introducer(d2));
    CHECK(!proto::get_introducer(d3));

    CHECK(proto::get_name(d1) == "hp-note");
    CHECK(proto::get_name(d2) == "localhost");
    CHECK(proto::get_name(d3) == "hp-note");

    CHECK(proto::get_addresses_size(d1) == 1);
    CHECK(proto::get_addresses(d1, 0) == "dynamic");

    CHECK(proto::get_addresses_size(d2) == 1);
    CHECK(proto::get_addresses(d2, 0) == "dynamic");

    SECTION("corrupt data a little bit") {
        buff_raw[11] = 0xC0;
        auto buff = utils::bytes_view_t(buff_raw, buff_sz);
        auto r = parse_bep(buff);
        REQUIRE(!r);
        CHECK(r.error() == utils::make_error_code(utils::bep_error_code_t::lz4_decoding));
    }

    SECTION("serialize, round-trip") {
        auto buff = serialize(msg);
        auto r2 = parse_bep(buff);
        REQUIRE(r2);
        auto &v2 = r.value();
        auto &msg2 = std::get<proto::ClusterConfig>(v.message);
        CHECK(v2.consumed == buff_sz);
        auto f_1 = proto::get_folders(msg2, 0);
        auto d_1 = proto::get_devices(f_1, 0);
        auto d_1_id = proto::get_id(d_1);
        CHECK(d_1_id == proto::get_id(d1));
    }

    SECTION("serialize compressed, round-trip") {
        auto buff = serialize(msg, proto::Compression::ALWAYS);
        REQUIRE(buff.size() <= buff_sz + 10);
        auto r2 = parse_bep(buff);
        REQUIRE(r2);
        auto &v2 = r2.value();
        auto &msg2 = std::get<proto::ClusterConfig>(v2.message);
        CHECK(v2.consumed == buff.size());
        auto f_1 = proto::get_folders(msg2, 0);
        auto d_1 = proto::get_devices(f_1, 0);
        auto d_1_id = proto::get_id(d_1);
        CHECK(d_1_id == proto::get_id(d1));
    }
}
