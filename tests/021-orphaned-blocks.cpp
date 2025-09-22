// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "model/device.h"
#include "model/cluster.h"
#include "model/misc/orphaned_blocks.h"
#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

namespace bfs = std::filesystem;

TEST_CASE("orphaned blocks, all removed for single file", "[model]") {
    test::init_logging();

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);

    auto builder = diff_builder_t(*cluster);

    REQUIRE(builder.upsert_folder("fid", "/some/path").apply());

    auto b1_hash = utils::sha256_digest(as_bytes("12345")).value();
    auto b2_hash = utils::sha256_digest(as_bytes("56789")).value();
    auto b3_hash = utils::sha256_digest(as_bytes("00000")).value();

    SECTION("1 file with 2 different blocsk erased") {
        proto::FileInfo pr_file;
        proto::set_name(pr_file, "a.txt");
        proto::set_block_size(pr_file, 5ul);
        proto::set_size(pr_file, 10ul);

        auto b1 = proto::BlockInfo();
        proto::set_hash(b1, b1_hash);
        proto::set_offset(b1, 5u);
        proto::set_size(b1, 5);
        proto::add_blocks(pr_file, b1);

        auto b2 = proto::BlockInfo();
        proto::set_hash(b2, b2_hash);
        proto::set_offset(b2, 5u);
        proto::set_size(b2, 5);
        proto::add_blocks(pr_file, b2);

        auto bi1 = model::block_info_t::create(b1).value();
        auto bi2 = model::block_info_t::create(b2).value();

        REQUIRE(builder.local_update("fid", pr_file).apply());
        auto &folder_infos = cluster->get_folders().by_id("fid")->get_folder_infos();
        auto &file_infos = folder_infos.by_device(*my_device)->get_file_infos();
        auto file = file_infos.by_name("a.txt");
        REQUIRE(file);

        auto orphans = model::orphaned_blocks_t();
        orphans.record(*file);

        auto blocks = orphans.deduce();
        REQUIRE(blocks.size() == 2);

        CHECK(blocks.contains(bi1->get_hash()));
        CHECK(blocks.contains(bi2->get_hash()));
    }

    SECTION("1 file with 2 same blocks erased") {
        proto::FileInfo pr_file;
        proto::set_name(pr_file, "a.txt");
        proto::set_block_size(pr_file, 5ul);
        proto::set_size(pr_file, 10ul);

        auto b1 = proto::BlockInfo();
        proto::set_hash(b1, b1_hash);
        proto::set_offset(b1, 5u);
        proto::set_size(b1, 5);
        proto::add_blocks(pr_file, b1);

        auto b2 = proto::BlockInfo();
        proto::set_hash(b2, b1_hash);
        proto::set_offset(b2, 5u);
        proto::set_size(b2, 5);
        proto::add_blocks(pr_file, b2);

        auto bi = model::block_info_t::create(b1).value();

        REQUIRE(builder.local_update("fid", pr_file).apply());
        auto &folder_infos = cluster->get_folders().by_id("fid")->get_folder_infos();
        auto &file_infos = folder_infos.by_device(*my_device)->get_file_infos();
        auto file = file_infos.by_name("a.txt");
        REQUIRE(file);

        auto orphans = model::orphaned_blocks_t();
        orphans.record(*file);

        auto blocks = orphans.deduce();
        REQUIRE(blocks.size() == 1);

        CHECK(blocks.contains(bi->get_hash()));
    }

    SECTION("2 file with 1 shared erased") {
        proto::FileInfo pr_file_1;
        proto::set_name(pr_file_1, "a.txt");
        proto::set_block_size(pr_file_1, 5ul);
        proto::set_size(pr_file_1, 10ul);

        auto b_1_1 = [&]() {
            auto b = proto::BlockInfo();
            proto::set_hash(b, b1_hash);
            proto::set_offset(b, 0);
            proto::set_size(b, 5);
            proto::add_blocks(pr_file_1, b);
            return b;
        }();

        auto b_1_2 = [&]() {
            auto b = proto::BlockInfo();
            proto::set_hash(b, b2_hash);
            proto::set_offset(b, 5);
            proto::set_size(b, 5);
            proto::add_blocks(pr_file_1, b);
            return b;
        }();

        proto::FileInfo pr_file_2;
        proto::set_name(pr_file_2, "b.txt");
        proto::set_block_size(pr_file_2, 5ul);
        proto::set_size(pr_file_2, 10ul);

        auto b_2_1 = [&]() {
            auto b = proto::BlockInfo();
            proto::set_hash(b, b1_hash);
            proto::set_offset(b, 0);
            proto::set_size(b, 5);
            proto::add_blocks(pr_file_2, b);
            return b;
        }();

        auto b_2_2 = [&]() {
            auto b = proto::BlockInfo();
            proto::set_hash(b, b3_hash);
            proto::set_offset(b, 5);
            proto::set_size(b, 5);
            proto::add_blocks(pr_file_2, b);
            return b;
        }();

        auto bi_1 = model::block_info_t::create(b_1_1).value();
        auto bi_2 = model::block_info_t::create(b_1_2).value();
        auto bi_3 = model::block_info_t::create(b_2_2).value();

        REQUIRE(builder.local_update("fid", pr_file_1).apply());
        REQUIRE(builder.local_update("fid", pr_file_2).apply());

        auto &folder_infos = cluster->get_folders().by_id("fid")->get_folder_infos();
        auto &file_infos = folder_infos.by_device(*my_device)->get_file_infos();
        auto file_1 = file_infos.by_name("a.txt");
        auto file_2 = file_infos.by_name("b.txt");
        REQUIRE(file_1);
        REQUIRE(file_2);

        auto orphans = model::orphaned_blocks_t();
        orphans.record(*file_1);

        auto blocks = orphans.deduce();
        REQUIRE(blocks.size() == 1);

        CHECK(blocks.contains(bi_2->get_hash()));

        orphans.record(*file_2);
        blocks = orphans.deduce();
        REQUIRE(blocks.size() == 3);
        CHECK(blocks.contains(bi_1->get_hash()));
        CHECK(blocks.contains(bi_2->get_hash()));
        CHECK(blocks.contains(bi_3->get_hash()));
    }
}
