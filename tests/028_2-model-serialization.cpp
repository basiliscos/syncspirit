// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include "test-utils.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::test;

using Catch::Matchers::Matches;

TEST_CASE("device serialization", "[model]") {
    auto id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto device = device_t::create(id, "my-device").value();
    auto uris = device_t::uris_t{};
    uris.emplace_back(utils::parse("tcp://1.2.3.4:5"));
    uris.emplace_back(utils::parse("tcp://6.7.8.9:10"));
    device->set_static_uris(uris);

    auto bytes = device->serialize();
    REQUIRE(bytes.size());

    auto db_device = db::Device();
    auto left = db::decode(bytes, db_device);
    REQUIRE(left == 0);

    auto device_opt = device_t::create(device->get_key(), db_device);
    REQUIRE(device_opt);
    auto &device_restored = device_opt.assume_value();
    CHECK(device_restored->get_name() == "my-device");
    CHECK(device_restored->device_id() == device->device_id());
    REQUIRE(device_restored->get_static_uris().size() == 2);
    REQUIRE(*device_restored->get_static_uris()[0] == *uris[0]);
    REQUIRE(*device_restored->get_static_uris()[1] == *uris[1]);
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
