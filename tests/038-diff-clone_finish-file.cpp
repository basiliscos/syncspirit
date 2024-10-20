// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "diff-builder.h"
#include "model/cluster.h"
#include "model/misc/sequencer.h"

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
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("1234-5678", "/my/path").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());

    auto &blocks_map = cluster->get_blocks();
    auto folder = cluster->get_folders().by_id("1234-5678");
    auto folder_my = folder->get_folder_infos().by_device(*my_device);
    auto folder_peer = folder->get_folder_infos().by_device(*peer_device);

    proto::FileInfo file_info;
    file_info.set_name("a.txt");
    file_info.set_sequence(folder_peer->get_max_sequence() + 1);
    auto version = file_info.mutable_version();
    auto counter = version->add_counters();
    counter->set_value(1);
    counter->set_id(peer_device->as_uint());

    SECTION("trivial cases") {
        SECTION("no file on my side, clone blockless file") {
            auto file_peer = file_info_t::create(sequencer->next_uuid(), file_info, folder_peer).value();
            REQUIRE(folder_peer->add_strict(file_peer));
            REQUIRE(builder.clone_file(*file_peer).apply());
            auto file_my = folder_my->get_file_infos().by_name(file_info.name());
            REQUIRE(file_my);
            CHECK(file_my->is_locally_available());
            CHECK(file_my->get_sequence() == 1);
            CHECK(file_my->get_folder_info() == folder_my.get());
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

            auto file_my = file_info_t::create(sequencer->next_uuid(), file_info, folder_my).value();
            file_my->assign_block(bi, 0);
            file_my->mark_local_available(0);
            REQUIRE(folder_my->add_strict(file_my));

            file_info.set_modified_s(123);
            auto file_peer = file_info_t::create(sequencer->next_uuid(), file_info, folder_peer).value();
            REQUIRE(folder_peer->add_strict(file_peer));
            file_peer->assign_block(bi, 0);
            file_peer->mark_local_available(0);
            REQUIRE(builder.clone_file(*file_peer).apply());

            file_my = folder_my->get_file_infos().by_name(file_info.name());
            REQUIRE(file_my);
            CHECK(file_my->is_locally_available());
            CHECK(file_my->get_sequence() == 2);
            CHECK(file_my->get_modified_s() == 123);
            CHECK(file_my->get_folder_info() == folder_my.get());
            CHECK(folder_my->get_max_sequence() == 2);
        }
    }
}
