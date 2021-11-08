#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/peer/peer_state.h"
#include "model/diff/diff_visitor.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;


TEST_CASE("peer state update", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device =  device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device =  device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    rotor::address_ptr_t addr;
    auto diff = diff::cluster_diff_ptr_t(new diff::peer::peer_state_t(peer_id.get_sha256(), addr, true));
    CHECK(peer_device->is_online() == false);

    REQUIRE(diff->apply(*cluster));
    CHECK(peer_device->is_online() == true);

    diff = new diff::peer::peer_state_t(peer_id.get_sha256(), addr, false);
    REQUIRE(diff->apply(*cluster));
    CHECK(peer_device->is_online() == false);
}
