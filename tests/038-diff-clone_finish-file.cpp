// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

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

    auto pr_file = proto::FileInfo();
    proto::set_name(pr_file, "a.txt");
    proto::set_sequence(pr_file, folder_peer->get_max_sequence() + 1);
    auto &version = proto::get_version(pr_file);
    auto &counter = proto::add_counters(version);

    proto::set_value(counter, 1);
    proto::set_id(counter, peer_device->device_id().get_uint());

    SECTION("trivial cases") {
        SECTION("no file on my side, clone blockless file") {
            auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
            REQUIRE(folder_peer->add_strict(file_peer));
            REQUIRE(builder.remote_copy(*file_peer, *folder_peer).apply());
            auto file_my = folder_my->get_file_infos().by_name(proto::get_name(pr_file));
            REQUIRE(file_my);
            CHECK(file_my->is_locally_available());
            CHECK(file_my->get_sequence() == 1);
            CHECK(folder_my->get_max_sequence() == 1);
        }

        SECTION("with blocks") {
            proto::set_size(pr_file, 5ul);
            proto::set_block_size(pr_file, 5ul);

            auto &b = proto::add_blocks(pr_file);
            proto::set_hash(b, as_bytes("12345"));
            proto::set_size(b, 5);
            auto bi = model::block_info_t::create(b).value();
            blocks_map.put(bi);

            auto file_my = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
            file_my->assign_block(bi, 0);
            file_my->mark_local_available(0);
            REQUIRE(folder_my->add_strict(file_my));

            SECTION("with same blocks as peer") {
                proto::set_modified_s(pr_file, 123);
                auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
                REQUIRE(folder_peer->add_strict(file_peer));
                file_peer->assign_block(bi, 0);
                file_peer->mark_local_available(0);
                REQUIRE(builder.remote_copy(*file_peer, *folder_peer).apply());

                file_my = folder_my->get_file_infos().by_name(proto::get_name(pr_file));
                REQUIRE(file_my);
                CHECK(file_my->is_locally_available());
                CHECK(file_my->get_sequence() == 2);
                CHECK(file_my->get_modified_s() == 123);
                CHECK(folder_my->get_max_sequence() == 2);
            }

            SECTION("emty peer file") {
                proto::set_size(pr_file, 0);
                proto::set_block_size(pr_file, 0);
                auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
                REQUIRE(folder_peer->add_strict(file_peer));
                REQUIRE(builder.remote_copy(*file_peer, *folder_peer).apply());

                file_my = folder_my->get_file_infos().by_name(proto::get_name(pr_file));
                REQUIRE(file_my);
                CHECK(file_my->is_locally_available());
                CHECK(file_my->get_sequence() == 2);
                CHECK(file_my->get_blocks().size() == 0);
                CHECK(folder_my->get_max_sequence() == 2);
                CHECK(blocks_map.size() == 0);
            }
        }
    }
}
