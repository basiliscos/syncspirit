// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "diff-builder.h"
#include "model/cluster.h"
#include "model/diff/modify/lock_file.h"
#include "model/diff/local/file_availability.h"
#include "model/diff/contact/update_contact.h"
#include "model/diff/cluster_visitor.h"
#include <cstring>

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
    REQUIRE(peer_device->get_state() == model::device_state_t::offline);

    REQUIRE(builder.update_state(*peer_device, addr, device_state_t::online).apply());
    CHECK(peer_device->get_state() == model::device_state_t::online);

    REQUIRE(builder.update_state(*peer_device, addr, device_state_t::offline).apply());
    CHECK(peer_device->get_state() == model::device_state_t::offline);
}

TEST_CASE("with file", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);

    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = cluster->get_folders().by_id("1234-5678");
    auto folder_info = folder->get_folder_infos().by_device(*my_device);

    proto::FileInfo pr_file_info;
    pr_file_info.set_name("a.txt");
    pr_file_info.set_type(proto::FileInfoType::SYMLINK);
    pr_file_info.set_symlink_target("/some/where");
    pr_file_info.set_block_size(5);
    pr_file_info.set_size(5);
    auto b1_hash = utils::sha256_digest("12345").value();
    auto b1 = pr_file_info.add_blocks();
    b1->set_hash(b1_hash);
    b1->set_offset(0);
    b1->set_size(5);

    REQUIRE(builder.local_update(folder->get_id(), pr_file_info).apply());
    auto file = folder_info->get_file_infos().by_name("a.txt");
    REQUIRE(file);

    auto v = file->get_version();
    REQUIRE(v->counters_count() == 1);
    REQUIRE(v->get_counter(0).id() == my_device->as_uint());

    SECTION("lock/unlock") {
        auto diff = diff::cluster_diff_ptr_t(new diff::modify::lock_file_t(*file, true));
        REQUIRE(diff->apply(*cluster));
        auto file = folder_info->get_file_infos().by_name(pr_file_info.name());
        REQUIRE(file->is_locked());

        diff = diff::cluster_diff_ptr_t(new diff::modify::lock_file_t(*file, false));
        REQUIRE(diff->apply(*cluster));
        REQUIRE(!file->is_locked());
    }

    SECTION("file_availability") {
        auto block = cluster->get_blocks().get(b1_hash);
        file->remove_blocks();
        file->assign_block(block, 0);
        REQUIRE(!file->is_locally_available());
        auto diff = diff::cluster_diff_ptr_t(new diff::local::file_availability_t(file));
        REQUIRE(diff->apply(*cluster));
        REQUIRE(file->is_locally_available());
    }
}

TEST_CASE("update_contact_t", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    {
        auto update_my =
            diff::cluster_diff_ptr_t(new diff::contact::update_contact_t(*cluster, {"127.0.0.1", "127.0.0.1"}));
        REQUIRE(update_my->apply(*cluster));

        auto got_uris = my_device->get_uris();
        REQUIRE(got_uris.size() == 1u);
        REQUIRE(*got_uris.front() == *utils::parse("tcp://127.0.0.1:0/"));
    }

    {
        auto url_1 = utils::parse("tcp://192.168.100.6:22000");
        auto url_2 = utils::parse("tcp://192.168.100.6:22001");
        auto url_3 = utils::parse("tcp://192.168.100.6:22001");
        auto update_peer = diff::cluster_diff_ptr_t(
            new diff::contact::update_contact_t(*cluster, peer_id, utils::uri_container_t{url_1, url_1, url_2, url_3}));
        REQUIRE(update_peer->apply(*cluster));
        auto &uris = peer_device->get_uris();
        REQUIRE(uris.size() == 2u);
        CHECK(*uris[0] == *url_1);
        CHECK(*uris[1] == *url_2);
    };
}
