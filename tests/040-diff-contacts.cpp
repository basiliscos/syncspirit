// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

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

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    auto &devices = cluster->get_devices();
    devices.put(my_device);

    db::SomeDevice db_device;
    db::set_name(db_device, "a name");
    auto buider = diff_builder_t(*cluster);
    REQUIRE(buider.add_unknown_device(peer_id, db_device).apply());

    db::set_name(db_device, "a name-2");
    auto diff = model::diff::cluster_diff_ptr_t{};
    diff = new model::diff::contact::unknown_connected_t(*cluster, peer_id, db_device);
    REQUIRE(diff->apply(*cluster, *controller, {}));

    REQUIRE(cluster->get_pending_devices().size() == 1);
    auto unknown = cluster->get_pending_devices().by_sha256(peer_id.get_sha256());
    REQUIRE(unknown);
    CHECK(unknown->get_name() == "a name-2");
}

TEST_CASE("unknown device is removed when connecting to it ", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto &devices = cluster->get_devices();
    devices.put(my_device);

    db::SomeDevice db_device;
    db::set_name(db_device, "a name");
    auto buider = diff_builder_t(*cluster);
    REQUIRE(buider.add_unknown_device(peer_id, db_device).apply());
    REQUIRE(cluster->get_pending_devices().size() == 1);
    REQUIRE(cluster->get_devices().size() == 1);

    REQUIRE(buider.update_peer(peer_id).apply());
    CHECK(cluster->get_pending_devices().size() == 0);
    CHECK(cluster->get_devices().size() == 2);
}

TEST_CASE("ignored device connected", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    auto &devices = cluster->get_devices();
    devices.put(my_device);

    db::SomeDevice db_device;
    db::set_name(db_device, "a name");
    auto buider = diff_builder_t(*cluster);
    REQUIRE(buider.add_ignored_device(peer_id, db_device).apply());
    auto &ignored_devices = cluster->get_ignored_devices();
    auto ignored_device = ignored_devices.by_sha256(peer_id.get_sha256());

    auto diff = model::diff::cluster_diff_ptr_t{};
    diff = new model::diff::contact::ignored_connected_t(*cluster, peer_id, db_device);
    REQUIRE(diff->apply(*cluster, *controller, {}));
}

TEST_CASE("ignored device is removed when connecting to it ", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto &devices = cluster->get_devices();
    devices.put(my_device);

    db::SomeDevice db_device;
    db::set_name(db_device, "a name");
    auto buider = diff_builder_t(*cluster);
    REQUIRE(buider.add_ignored_device(peer_id, db_device).apply());
    REQUIRE(cluster->get_ignored_devices().size() == 1);
    REQUIRE(cluster->get_devices().size() == 1);

    REQUIRE(buider.update_peer(peer_id).apply());
    CHECK(cluster->get_ignored_devices().size() == 0);
    CHECK(cluster->get_devices().size() == 2);
}

TEST_CASE("unknown device is removed adding the same ignored device", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto &devices = cluster->get_devices();
    devices.put(my_device);

    db::SomeDevice db_device;
    db::set_name(db_device, "a name");
    auto buider = diff_builder_t(*cluster);
    REQUIRE(buider.add_unknown_device(peer_id, db_device).apply());
    REQUIRE(cluster->get_pending_devices().size() == 1);
    REQUIRE(cluster->get_ignored_devices().size() == 0);
    REQUIRE(cluster->get_devices().size() == 1);

    REQUIRE(buider.add_ignored_device(peer_id, db_device).apply());
    REQUIRE(cluster->get_pending_devices().size() == 0);
    REQUIRE(cluster->get_ignored_devices().size() == 1);
    REQUIRE(cluster->get_devices().size() == 1);
}
