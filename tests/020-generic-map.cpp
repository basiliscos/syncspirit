#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/device.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

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
