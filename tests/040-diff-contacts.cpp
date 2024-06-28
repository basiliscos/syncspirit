// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "diff-builder.h"

#include "model/cluster.h"
#include "model/diff/contact/ignored_connected.h"
#include "model/diff/contact/unknown_connected.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("unknown device connected", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1, 1));
    auto &devices = cluster->get_devices();
    devices.put(my_device);

    db::SomeDevice db_device;
    auto unknown_device = unknown_device_t::create(peer_id, db_device).value();
    auto diff = model::diff::contact_diff_ptr_t{};
    auto diff_raw = new model::diff::contact::unknown_connected_t(*cluster, *unknown_device);
    CHECK(diff_raw->inner);
    diff = diff_raw;
    REQUIRE(diff->apply(*cluster));

    REQUIRE(cluster->get_unknown_devices().size() == 1);
    auto unknown = cluster->get_unknown_devices().by_sha256(peer_id.get_sha256());
    REQUIRE(unknown);

    auto delta = pt::microsec_clock::local_time() - unknown->get_last_seen();
    CHECK(delta.seconds() <= 2);

    diff_raw = new model::diff::contact::unknown_connected_t(*cluster, *unknown_device);
    CHECK(!diff_raw->inner);
    diff = diff_raw;
    REQUIRE(diff->apply(*cluster));

    unknown = cluster->get_unknown_devices().by_sha256(peer_id.get_sha256());
    REQUIRE(unknown);

    delta = pt::microsec_clock::local_time() - unknown->get_last_seen();
    CHECK(delta.seconds() <= 2);
}

TEST_CASE("ignored device connected", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1, 1));
    auto &devices = cluster->get_devices();
    devices.put(my_device);

    db::SomeDevice db_device;
    auto buider = diff_builder_t(*cluster);
    REQUIRE(buider.add_ignored_device(peer_id, db_device).apply());
    auto &ignored_devices = cluster->get_ignored_devices();
    auto ignored_device = ignored_devices.by_sha256(peer_id.get_sha256());

    auto diff = model::diff::contact_diff_ptr_t{};
    diff = new model::diff::contact::ignored_connected_t(*cluster, *ignored_device);
    REQUIRE(diff->apply(*cluster));

    auto delta = pt::microsec_clock::local_time() - ignored_device->get_last_seen();
    CHECK(delta.seconds() <= 2);
}
