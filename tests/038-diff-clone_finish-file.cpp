// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/clone_file.h"
#include "model/diff/modify/finish_file.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("new file diff", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
    REQUIRE(diff->apply(*cluster));

    diff = new diff::modify::share_folder_t(peer_id.get_sha256(), db_folder.id());
    REQUIRE(diff->apply(*cluster));

    auto &blocks_map = cluster->get_blocks();
    auto folder = cluster->get_folders().by_id(db_folder.id());
    auto folder_my = folder->get_folder_infos().by_device(my_device);
    auto folder_peer = folder->get_folder_infos().by_device(peer_device);

    proto::FileInfo file_info;
    file_info.set_name("a.txt");
    auto version = file_info.mutable_version();
    auto counter = version->add_counters();
    counter->set_value(1);
    counter->set_id(peer_device->as_uint());

    SECTION("trivial cases") {
        SECTION("no file on my side, clone blockless file") {
            auto file_peer = file_info_t::create(cluster->next_uuid(), file_info, folder_peer).value();
            folder_peer->add(file_peer);
            diff = new diff::modify::clone_file_t(*file_peer);
            REQUIRE(diff->apply(*cluster));
            auto file_my = folder_my->get_file_infos().by_name(file_info.name());
            REQUIRE(file_my);
            CHECK(file_my->is_locally_available());
            CHECK(file_my->get_sequence() == 1);
            CHECK(file_my->get_folder_info() == folder_my.get());
            CHECK(!file_my->get_source());
            CHECK(folder_my->get_max_sequence() == 1);
        }

        SECTION("file with the same blocks already exists on my side") {
            file_info.set_size(5);
            file_info.set_block_size(5);
            auto b = file_info.add_blocks();
            b->set_hash(utils::sha256_digest("12345").value());
            b->set_size(5);
            auto bi = model::block_info_t::create(*b).value();
            blocks_map.put(bi);

            auto file_my = file_info_t::create(cluster->next_uuid(), file_info, folder_peer).value();
            file_my->assign_block(bi, 0);
            file_my->mark_local_available(0);
            folder_my->add(file_my);

            file_info.set_modified_s(123);
            auto file_peer = file_info_t::create(cluster->next_uuid(), file_info, folder_peer).value();
            folder_peer->add(file_peer);
            file_peer->assign_block(bi, 0);
            diff = new diff::modify::clone_file_t(*file_peer);
            REQUIRE(diff->apply(*cluster));

            file_my = folder_my->get_file_infos().by_name(file_info.name());
            REQUIRE(file_my);
            CHECK(file_my->is_locally_available());
            CHECK(file_my->get_sequence() == 1);
            CHECK(file_my->get_modified_s() == 123);
            CHECK(file_my->get_folder_info() == folder_my.get());
            CHECK(!file_my->get_source());
            CHECK(folder_my->get_max_sequence() == 1);
        }
    }

    SECTION("non-trivial cases") {
        file_info.set_size(5);
        file_info.set_block_size(5);
        auto b = file_info.add_blocks();
        b->set_hash(utils::sha256_digest("12345").value());
        b->set_size(5);
        auto bi = model::block_info_t::create(*b).value();
        blocks_map.put(bi);

        auto file_my = file_info_ptr_t{};
        auto file_peer = file_info_ptr_t{};

        SECTION("new file with blocks") {
            file_info.set_modified_s(123);
            file_peer = file_info_t::create(cluster->next_uuid(), file_info, folder_peer).value();
            file_peer->assign_block(bi, 0);
            folder_peer->add(file_peer);
            diff = new diff::modify::clone_file_t(*file_peer);
            REQUIRE(diff->apply(*cluster));

            file_my = folder_my->get_file_infos().by_name(file_info.name());
            REQUIRE(file_my);
            CHECK(!file_my->is_locally_available());
            CHECK(file_my->get_sequence() == 0);
            CHECK(file_my->get_modified_s() == 123);
            CHECK(file_my->get_folder_info() == folder_my.get());
            CHECK(file_my->get_source() == file_peer);
            CHECK(folder_my->get_max_sequence() == 0);
        }

        SECTION("existing file with blocks") {
            file_my = file_info_t::create(cluster->next_uuid(), file_info, folder_my).value();
            file_my->assign_block(bi, 0);
            file_my->mark_local_available(0);
            folder_my->add(file_my);

            file_info.set_modified_s(123);
            file_peer = file_info_t::create(cluster->next_uuid(), file_info, folder_peer).value();

            auto b2 = proto::BlockInfo{};
            b2.set_hash(utils::sha256_digest("67890").value());
            b2.set_size(5);
            auto bi2 = model::block_info_t::create(b2).value();
            blocks_map.put(bi2);

            file_peer->assign_block(bi2, 0);
            folder_peer->add(file_peer);
            diff = new diff::modify::clone_file_t(*file_peer);
            REQUIRE(diff->apply(*cluster));

            file_my = folder_my->get_file_infos().by_name(file_info.name());
            REQUIRE(file_my);
            CHECK(file_my->is_locally_available());
            CHECK(file_my->get_sequence() == 0);
            CHECK(file_my->get_modified_s() == 0);
            CHECK(file_my->get_source() == file_peer);
            CHECK(file_my->get_blocks().at(0) == bi);
            CHECK(file_my->get_folder_info() == folder_my.get());
            CHECK(folder_my->get_max_sequence() == 0);
        }

        file_peer->mark_local_available(0);
        diff = new diff::modify::finish_file_t(*file_my);
        REQUIRE(diff->apply(*cluster));

        file_my = folder_my->get_file_infos().by_name(file_info.name());
        CHECK(file_my->is_locally_available());
        CHECK(!file_my->get_source());
        CHECK(file_my->get_sequence() == 1);
        CHECK(file_my->get_modified_s() == 123);
        CHECK(folder_my->get_max_sequence() == 1);
    }
}
