// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "diff-builder.h"
#include "model/cluster.h"
#include "model/diff/modify/lock_file.h"
#include "model/diff/local/file_availability.h"
#include "model/diff/contact/update_contact.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("peer state update", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    rotor::address_ptr_t addr;
    auto builder = diff_builder_t(*cluster);
    REQUIRE(peer_device->get_state().get_connection_state() == model::connection_state_t::offline);

    auto state_1 = peer_device->get_state().connecting().connected().online("tcp://1.2.3.4:5678");
    REQUIRE(builder.update_state(*peer_device, addr, state_1).apply());
    REQUIRE(peer_device->get_state().is_online());

    REQUIRE(builder.update_state(*peer_device, addr, state_1.offline()).apply());
    REQUIRE(peer_device->get_state().is_offline());
}

TEST_CASE("with file", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    cluster->get_devices().put(my_device);

    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = cluster->get_folders().by_id("1234-5678");
    auto folder_info = folder->get_folder_infos().by_device(*my_device);

    auto pr_file = []() -> proto::FileInfo {
        auto f = proto::FileInfo();
        proto::set_name(f, "a.txt");
        proto::set_block_size(f, 5);
        proto::set_size(f, 5);
        return f;
    }();
    auto b1_hash = utils::sha256_digest(as_bytes("12345")).value();
    auto &b1 = proto::add_blocks(pr_file);
    proto::set_hash(b1, b1_hash);
    proto::set_offset(b1, 0);
    proto::set_size(b1, 5);

    REQUIRE(builder.local_update(folder->get_id(), pr_file).apply());
    auto file = folder_info->get_file_infos().by_name("a.txt");
    REQUIRE(file);

    auto v = file->get_version();
    REQUIRE(v.counters_size() == 1);
    REQUIRE(proto::get_id(v.get_counter(0)) == my_device->device_id().get_uint());

    SECTION("lock/unlock") {
        auto diff = diff::cluster_diff_ptr_t(new diff::modify::lock_file_t(*file, true));
        REQUIRE(diff->apply(*controller, {}));
        auto name = proto::get_name(pr_file);
        auto file = folder_info->get_file_infos().by_name(name);
        REQUIRE(file->is_locked());

        diff = diff::cluster_diff_ptr_t(new diff::modify::lock_file_t(*file, false));
        REQUIRE(diff->apply(*controller, {}));
        REQUIRE(!file->is_locked());
    }

    SECTION("file_availability") {
        auto block = cluster->get_blocks().by_hash(b1_hash);
        file->remove_blocks();
        file->assign_block(block, 0);
        REQUIRE(!file->is_locally_available());
        auto diff = diff::cluster_diff_ptr_t(new diff::local::file_availability_t(file));
        REQUIRE(diff->apply(*controller, {}));
        REQUIRE(file->is_locally_available());
    }
}

TEST_CASE("update_contact_t", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    {
        auto url_1 = utils::parse("tcp://192.168.100.6:22000");
        auto url_2 = utils::parse("tcp://192.168.100.6:22001");
        auto url_3 = utils::parse("tcp://192.168.100.6:22001");
        auto update_peer = diff::cluster_diff_ptr_t(
            new diff::contact::update_contact_t(*cluster, peer_id, utils::uri_container_t{url_1, url_1, url_2, url_3}));
        REQUIRE(update_peer->apply(*controller, {}));
        auto &uris = peer_device->get_uris();
        REQUIRE(uris.size() == 2u);
        CHECK(*uris[0] == *url_1);
        CHECK(*uris[1] == *url_2);
    };
}
