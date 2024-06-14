// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "diff-builder.h"

#include "model/cluster.h"
#include "model/diff/modify/remove_peer.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("remove peer", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto &blocks_map = cluster->get_blocks();
    auto builder = diff_builder_t(*cluster);
    builder.create_folder("1234-5678", "some/path", "my-label");
    builder.share_folder(peer_id.get_sha256(), "1234-5678");
    REQUIRE(builder.apply());

    SECTION("1 file, 1 folder, 1 block") {
        auto bi = proto::BlockInfo();
        bi.set_size(5);
        bi.set_hash(utils::sha256_digest("12345").value());
        auto block = block_info_t::create(bi).assume_value();
        blocks_map.put(block);

        auto folder = cluster->get_folders().by_id("1234-5678");
        auto folder_info = folder->get_folder_infos().by_device(*peer_device);
        REQUIRE(folder_info);

        proto::FileInfo pr_fi;
        pr_fi.set_name("a.txt");
        pr_fi.set_block_size(5);
        pr_fi.set_size(5);
        auto b1_hash = utils::sha256_digest("12345").value();
        auto b1 = pr_fi.add_blocks();
        b1->set_hash(b1_hash);
        b1->set_offset(0);
        b1->set_size(5);

        auto fi = file_info_t::create(cluster->next_uuid(), pr_fi, folder_info).value();
        fi->assign_block(block, 0);
        folder_info->get_file_infos().put(fi);

        REQUIRE(cluster->get_blocks().size() == 1);
        REQUIRE(cluster->get_devices().size() == 2);

        auto diff = diff::cluster_diff_ptr_t(new diff::modify::remove_peer_t(*cluster, *peer_device));
        REQUIRE(diff->apply(*cluster));

        CHECK(cluster->get_blocks().size() == 0);
        CHECK(cluster->get_devices().size() == 1);
        CHECK(!folder->is_shared_with(*peer_device));
    }

    SECTION("3 files, 2 shared files, 2 shared folders, 3 block, 2 shared blocks") {
        builder.create_folder("1234", "some-2/path-2", "my-label-2");
        builder.share_folder(peer_id.get_sha256(), "1234");
        REQUIRE(builder.apply());

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

        auto f1 = cluster->get_folders().by_id("1234-5678");
        auto fi_1 = f1->get_folder_infos().by_device(*peer_device);
        REQUIRE(fi_1);

        auto f2 = cluster->get_folders().by_id("1234");
        auto fi_2 = f2->get_folder_infos().by_device(*peer_device);
        REQUIRE(fi_2);

        auto file_1 = [&]() {
            proto::FileInfo pr_fi;
            pr_fi.set_name("a.txt");
            pr_fi.set_block_size(5);
            pr_fi.set_size(5);
            auto b_hash = utils::sha256_digest("1").value();
            auto b = pr_fi.add_blocks();
            b->set_hash(b_hash);
            b->set_offset(0);
            b->set_size(5);

            auto fi = file_info_t::create(cluster->next_uuid(), pr_fi, fi_1).value();
            fi->assign_block(b1, 0);
            fi_1->get_file_infos().put(fi);
            return fi;
        }();

        auto file_2 = [&]() {
            proto::FileInfo pr_fi;
            pr_fi.set_name("b.txt");
            pr_fi.set_block_size(5);
            pr_fi.set_size(10);
            auto pr_b_1 = pr_fi.add_blocks();
            pr_b_1->set_hash(utils::sha256_digest("2").value());
            pr_b_1->set_offset(0);
            pr_b_1->set_size(5);

            auto pr_b_2 = pr_fi.add_blocks();
            pr_b_2->set_hash(utils::sha256_digest("1").value());
            pr_b_2->set_offset(0);
            pr_b_1->set_size(5);

            auto fi = file_info_t::create(cluster->next_uuid(), pr_fi, fi_2).value();
            fi->assign_block(b2, 0);
            fi->assign_block(b1, 1);
            fi_2->get_file_infos().put(fi);
            return fi;
        }();

        REQUIRE(cluster->get_blocks().size() == 3);
        REQUIRE(cluster->get_devices().size() == 2);

        auto diff = diff::cluster_diff_ptr_t(new diff::modify::remove_peer_t(*cluster, *peer_device));
        REQUIRE(diff->apply(*cluster));

        CHECK(cluster->get_blocks().size() == 1);
        CHECK(cluster->get_devices().size() == 1);
        CHECK(!f1->is_shared_with(*peer_device));
        CHECK(!f2->is_shared_with(*peer_device));
    }
}
