// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "diff-builder.h"
#include "model/misc/sequencer.h"
#include "model/cluster.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("remove peer", "[model]") {
    test::init_logging();

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    auto &devices = cluster->get_devices();
    devices.put(my_device);
    devices.put(peer_device);

    auto &blocks_map = cluster->get_blocks();
    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());

    SECTION("1 file, 1 folder, 1 block") {
        auto bi = proto::BlockInfo();
        proto::set_hash(bi, utils::sha256_digest(as_bytes("12345")).value());
        proto::set_size(bi, 5);
        auto block = block_info_t::create(bi).assume_value();
        blocks_map.put(block);

        auto folder = cluster->get_folders().by_id("1234-5678");
        auto folder_info = folder->get_folder_infos().by_device(*peer_device);
        REQUIRE(folder_info);

        proto::FileInfo pr_fi;
        proto::set_name(pr_fi, "a.txt");
        proto::set_block_size(pr_fi, 5ul);
        proto::set_size(pr_fi, 5ul);
        auto& v = proto::get_version(pr_fi);
        proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 0));

        auto b1_hash = utils::sha256_digest(as_bytes("12345")).value();
        auto& b1 = proto::add_blocks(pr_fi);
        proto::set_hash(b1, as_bytes("123"));
        proto::set_size(b1, 5);

        auto fi = file_info_t::create(sequencer->next_uuid(), pr_fi, folder_info).value();
        fi->assign_block(block, 0);
        folder_info->get_file_infos().put(fi);

        REQUIRE(cluster->get_blocks().size() == 1);
        REQUIRE(cluster->get_devices().size() == 2);

        REQUIRE(builder.remove_peer(*peer_device).apply());
        CHECK(cluster->get_blocks().size() == 0);
        CHECK(cluster->get_devices().size() == 1);
        CHECK(!folder->is_shared_with(*peer_device));
        CHECK(devices.size() == 1);
    }

    SECTION("3 files, 2 shared files, 2 shared folders, 3 block, 2 shared blocks") {
        REQUIRE(builder.upsert_folder("1234", "some-2/path-2", "my-label-2").apply());
        REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234").apply());

        auto b1 = [&]() {
            auto bi = proto::BlockInfo();
            proto::set_hash(bi, utils::sha256_digest(as_bytes("1")).value());
            proto::set_size(bi, 5);
            auto block = block_info_t::create(bi).assume_value();
            blocks_map.put(block);
            return block;
        }();
        auto b2 = [&]() {
            auto bi = proto::BlockInfo();
            proto::set_hash(bi, utils::sha256_digest(as_bytes("2")).value());
            proto::set_size(bi, 5);
            auto block = block_info_t::create(bi).assume_value();
            blocks_map.put(block);
            return block;
        }();
        auto b3 = [&]() {
            auto bi = proto::BlockInfo();
            proto::set_hash(bi, utils::sha256_digest(as_bytes("3")).value());
            proto::set_size(bi, 5);
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
            proto::set_name(pr_fi, "a.txt");
            proto::set_block_size(pr_fi, 5ul);
            proto::set_size(pr_fi, 5ul);
            auto& v = proto::get_version(pr_fi);
            proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 0));

            auto b_hash = utils::sha256_digest(as_bytes("1")).value();
            auto& b = proto::add_blocks(pr_fi);
            proto::set_hash(b, b_hash);
            proto::set_size(b, 5);

            auto fi = file_info_t::create(sequencer->next_uuid(), pr_fi, fi_1).value();
            fi->assign_block(b1, 0);
            fi_1->get_file_infos().put(fi);
            return fi;
        }();

        auto file_2 = [&]() {
            proto::FileInfo pr_fi;
            proto::set_name(pr_fi, "b.txt");
            proto::set_block_size(pr_fi, 5ul);
            proto::set_size(pr_fi, 10ul);
            auto& v = proto::get_version(pr_fi);
            proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 0));

            auto b1_hash = utils::sha256_digest(as_bytes("2")).value();
            auto& pr_b1 = proto::add_blocks(pr_fi);
            proto::set_hash(pr_b1, b1_hash);
            proto::set_size(pr_b1, 5);
            proto::set_offset(pr_b1, 0);

            auto b2_hash = utils::sha256_digest(as_bytes("1")).value();
            auto& pr_b2 = proto::add_blocks(pr_fi);
            proto::set_hash(pr_b2, b2_hash);
            proto::set_size(pr_b2, 5);
            proto::set_offset(pr_b2, 0);

            auto fi = file_info_t::create(sequencer->next_uuid(), pr_fi, fi_2).value();
            fi->assign_block(b2, 0);
            fi->assign_block(b1, 1);
            fi_2->get_file_infos().put(fi);
            return fi;
        }();

        REQUIRE(cluster->get_blocks().size() == 3);
        REQUIRE(cluster->get_devices().size() == 2);

        REQUIRE(builder.remove_peer(*peer_device).apply());
        CHECK(cluster->get_blocks().size() == 1);
        CHECK(cluster->get_devices().size() == 1);
        CHECK(!f1->is_shared_with(*peer_device));
        CHECK(!f2->is_shared_with(*peer_device));
        CHECK(devices.size() == 1);
    }

    SECTION("unknown folders are removed") {
        db::PendingFolder db_pf;
        auto& db_f = db::get_folder(db_pf);
        db::set_id(db_f, "1234");
        auto& db_fi = db::get_folder_info(db_pf);
        db::set_max_sequence(db_fi, 5);
        db::set_index_id(db_fi, 10);

        auto uf = pending_folder_t::create(sequencer->next_uuid(), db_pf, peer_device->device_id()).value();
        auto &unknown_folders = cluster->get_pending_folders();
        unknown_folders.put(uf);
        REQUIRE(builder.remove_peer(*peer_device).apply());
        CHECK(devices.size() == 1);
    }
}
