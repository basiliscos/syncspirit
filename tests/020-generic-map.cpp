// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "model/device.h"
#include "model/misc/lru_cache.hpp"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

namespace bfs = std::filesystem;

TEST_CASE("generic map", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto map = devices_map_t();
    map.put(my_device);
    REQUIRE(map.by_sha256(my_id.get_sha256()));
    REQUIRE(map.by_key(my_device->get_key()));

    map.remove(my_device);
    REQUIRE(!map.by_sha256(my_id.get_sha256()));
    REQUIRE(!map.by_key(my_device->get_key()));
    REQUIRE(map == map);
}

namespace syncspirit::model::details {

template <> inline std::string_view get_lru_key<std::string>(const std::string &key) { return key; }

} // namespace syncspirit::model::details

TEST_CASE("lru cache", "[model]") {
    SECTION("string item") {
        mru_list_t<std::string> list(3);
        list.put("a");
        list.put("b");
        list.put("c");

        CHECK(list.get("a") == "a");
        CHECK(list.get("b") == "b");
        CHECK(list.get("c") == "c");
        CHECK(list.get("d") == "");

        list.put("d");
        CHECK(list.get("a") == "");
        CHECK(list.get("b") == "b");
        CHECK(list.get("c") == "c");
        CHECK(list.get("d") == "d");

        list.get("b");
        list.put("e");
        CHECK(list.get("a") == "");
        CHECK(list.get("b") == "b");
        CHECK(list.get("c") == "");
        CHECK(list.get("d") == "d");
        CHECK(list.get("e") == "e");

        list.put("e");
        CHECK(list.get("a") == "");
        CHECK(list.get("b") == "b");
        CHECK(list.get("c") == "");
        CHECK(list.get("d") == "d");
        CHECK(list.get("e") == "e");

        list.remove("d");
        CHECK(list.get("a") == "");
        CHECK(list.get("b") == "b");
        CHECK(list.get("c") == "");
        CHECK(list.get("d") == "");
        CHECK(list.get("e") == "e");

        list.put("c");
        CHECK(list.get("a") == "");
        CHECK(list.get("b") == "b");
        CHECK(list.get("c") == "c");
        CHECK(list.get("d") == "");
        CHECK(list.get("e") == "e");
    }
}

struct item_t {
    std::string key;
    int value;
};

using item_map_t = syncspirit::model::generic_map_t<item_t, 1>;

namespace syncspirit::model {

template <> SYNCSPIRIT_API inline std::string_view get_index<0>(const item_t &item) noexcept { return item.key; }

} // namespace syncspirit::model

TEST_CASE("generic map ops") {
    item_map_t map;
    auto i1 = item_t{"k", 1};
    auto i2 = item_t{"k", 2};

    map.put(i1);
    CHECK(map.get("k").value == 1);

    map.put(i2);
    CHECK(map.get("k").value == 2);
    REQUIRE(map.size() == 1);

    map.remove(i2);
    REQUIRE(map.size() == 0);
}
