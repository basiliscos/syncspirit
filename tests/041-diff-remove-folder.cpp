// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "diff-builder.h"

#include "model/cluster.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("remove folder", "[model]") {
    test::init_logging();

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto my_device = device_ptr_t{};
    my_device = new model::local_device_t(my_id, "my-device", "my-device");
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    auto &devices = cluster->get_devices();
    devices.put(my_device);
    devices.put(peer_device);

    auto &blocks_map = cluster->get_blocks();
    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("f1", "some/path-1", "my-label-1")
                .upsert_folder("f2", "some/path-2", "my-label-2")
                .apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "f1").apply());

    auto b1 = [&]() {
        auto bi = proto::BlockInfo();
        bi.set_size(5);
        bi.set_hash(utils::sha256_digest("1").value());
        auto block = block_info_t::create(bi).assume_value();
        blocks_map.put(block);
        return block;
    }();
    auto b2 = [&]() {
        auto bi = proto::BlockInfo();
        bi.set_size(5);
        bi.set_hash(utils::sha256_digest("2").value());
        auto block = block_info_t::create(bi).assume_value();
        blocks_map.put(block);
        return block;
    }();
    auto b3 = [&]() {
        auto bi = proto::BlockInfo();
        bi.set_size(5);
        bi.set_hash(utils::sha256_digest("3").value());
        auto block = block_info_t::create(bi).assume_value();
        blocks_map.put(block);
        return block;
    }();

    auto f1 = cluster->get_folders().by_id("f1");
    auto f2 = cluster->get_folders().by_id("f2");
    auto fi_1_peer = f1->get_folder_infos().by_device(*peer_device);
    auto fi_1_mine = f1->get_folder_infos().by_device(*my_device);
    auto fi_2_mine = f2->get_folder_infos().by_device(*my_device);

    auto file_1 = [&]() {
        proto::FileInfo pr_fi;
        pr_fi.set_name("a.txt");
        pr_fi.set_block_size(5);
        pr_fi.set_size(5);
        pr_fi.mutable_version()->add_counters()->set_id(peer_device->device_id().get_uint());
        auto b_hash = utils::sha256_digest("1").value();
        auto b = pr_fi.add_blocks();
        b->set_hash(b_hash);
        b->set_offset(0);
        b->set_size(5);

        auto fi = file_info_t::create(sequencer->next_uuid(), pr_fi, fi_1_peer).value();
        fi->assign_block(b1, 0);
        fi_1_peer->get_file_infos().put(fi);
        return fi;
    }();

    auto file_2 = [&]() {
        proto::FileInfo pr_fi;
        pr_fi.set_name("b.txt");
        pr_fi.set_block_size(5);
        pr_fi.set_size(5);
        pr_fi.mutable_version()->add_counters()->set_id(my_device->device_id().get_uint());
        auto b_hash = utils::sha256_digest("2").value();
        auto b = pr_fi.add_blocks();
        b->set_hash(b_hash);
        b->set_offset(0);
        b->set_size(5);

        auto fi = file_info_t::create(sequencer->next_uuid(), pr_fi, fi_1_mine).value();
        fi->assign_block(b2, 0);
        fi_1_mine->get_file_infos().put(fi);
        return fi;
    }();

    auto file_3 = [&]() {
        proto::FileInfo pr_fi;
        pr_fi.set_name("c.txt");
        pr_fi.set_block_size(5);
        pr_fi.set_size(5);
        pr_fi.mutable_version()->add_counters()->set_id(my_device->device_id().get_uint());
        auto b_hash = utils::sha256_digest("3").value();
        auto b = pr_fi.add_blocks();
        b->set_hash(b_hash);
        b->set_offset(0);
        b->set_size(5);

        auto fi = file_info_t::create(sequencer->next_uuid(), pr_fi, fi_2_mine).value();
        fi->assign_block(b3, 0);
        fi_2_mine->get_file_infos().put(fi);
        return fi;
    }();

    REQUIRE(cluster->get_blocks().size() == 3);
    REQUIRE(cluster->get_devices().size() == 2);
    REQUIRE(cluster->get_folders().size() == 2);

    SECTION("never exchanged info with a peer (index == 0") {
        REQUIRE(builder.remove_folder(*f1).remove_folder(*f2).apply());
        CHECK(cluster->get_pending_folders().size() == 0);
        CHECK(cluster->get_blocks().size() == 0);
        CHECK(cluster->get_folders().size() == 0);
    }

    SECTION("peer has seen the folder (index != 0)") {
        fi_1_peer->set_index(123);
        fi_1_peer->get_file_infos().put(file_1);
        REQUIRE(builder.remove_folder(*f1).remove_folder(*f2).apply());
        CHECK(cluster->get_pending_folders().size() == 1);
        CHECK(cluster->get_blocks().size() == 0);
        CHECK(cluster->get_folders().size() == 0);
    }
}
