#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/device.h"
#include "model/misc/lru_cache.hpp"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

namespace bfs = boost::filesystem;

TEST_CASE("generic map", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device =  device_t::create(my_id, "my-device").value();
    auto map = devices_map_t();
    map.put(my_device);
    REQUIRE(map.by_sha256(my_id.get_sha256()));
    REQUIRE(map.get(my_device->get_key()));

    map.remove(my_device);
    REQUIRE(!map.by_sha256(my_id.get_sha256()));
    REQUIRE(!map.get(my_device->get_key()));
}

namespace syncspirit::model::details {

template<>
inline std::string_view get_lru_key<std::string>(const std::string& key) {
    return key;
}

template<>
inline std::string_view get_lru_key<bfs::path>(const bfs::path& key) {
    return key.string();
}

}

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

    SECTION("string item") {
        mru_list_t<bfs::path> list(3);
        list.put("a");
        list.put("b");
        list.remove("b");

        CHECK(list.get("a") == bfs::path("a"));
        CHECK(list.get("b") == bfs::path());
    }
}

