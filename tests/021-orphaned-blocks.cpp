// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

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

namespace bfs = boost::filesystem;

TEST_CASE("orphaned blocks, all removed for single file", "[model]") {
    utils::set_default("trace");

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1, 1));
    cluster->get_devices().put(my_device);

    auto builder = diff_builder_t(*cluster);

    REQUIRE(builder.create_folder("fid", "/some/path").apply());

    auto b1_hash = utils::sha256_digest("12345").value();
    auto b2_hash = utils::sha256_digest("56789").value();
    auto b3_hash = utils::sha256_digest("00000").value();

    SECTION("1 file with 2 different blocsk erased") {
        proto::FileInfo pr_file;
        pr_file.set_name("a.txt");
        pr_file.set_block_size(5ul);
        pr_file.set_size(10ul);
        auto b1 = pr_file.add_blocks();
        b1->set_hash(b1_hash);
        b1->set_offset(0);
        b1->set_size(5);

        auto b2 = pr_file.add_blocks();
        b2->set_hash(b2_hash);
        b2->set_offset(5ul);
        b2->set_size(5);

        auto bi1 = model::block_info_t::create(*b1).value();
        auto bi2 = model::block_info_t::create(*b2).value();

        REQUIRE(builder.local_update("fid", pr_file).apply());
        auto& folder_infos =  cluster->get_folders().by_id("fid")->get_folder_infos();
        auto& file_infos = folder_infos.by_device(*my_device)->get_file_infos();
        auto file = file_infos.by_name("a.txt");
        REQUIRE(file);

        auto orphans = model::orphaned_blocks_t();
        orphans.record(*file);

        auto blocks = orphans.deduce();
        REQUIRE(blocks.size() == 2);
        CHECK(blocks.contains(bi1->get_key()));
        CHECK(blocks.contains(bi2->get_key()));
    }

    SECTION("1 file with 2 same blocks erased") {
        proto::FileInfo pr_file;
        pr_file.set_name("a.txt");
        pr_file.set_block_size(5ul);
        pr_file.set_size(10ul);
        auto b1 = pr_file.add_blocks();
        b1->set_hash(b1_hash);
        b1->set_offset(0);
        b1->set_size(5);

        auto b2 = pr_file.add_blocks();
        b2->set_hash(b1_hash);
        b2->set_offset(5ul);
        b2->set_size(5);

        auto bi = model::block_info_t::create(*b1).value();

        REQUIRE(builder.local_update("fid", pr_file).apply());
        auto& folder_infos =  cluster->get_folders().by_id("fid")->get_folder_infos();
        auto& file_infos = folder_infos.by_device(*my_device)->get_file_infos();
        auto file = file_infos.by_name("a.txt");
        REQUIRE(file);

        auto orphans = model::orphaned_blocks_t();
        orphans.record(*file);

        auto blocks = orphans.deduce();
        REQUIRE(blocks.size() == 1);
        CHECK(blocks.contains(bi->get_key()));
    }

    SECTION("2 file with 1 shared erased") {
        proto::FileInfo pr_file_1;
        pr_file_1.set_name("a.txt");
        pr_file_1.set_block_size(5ul);
        pr_file_1.set_size(10ul);
        auto b_1_1 = pr_file_1.add_blocks();
        b_1_1->set_hash(b1_hash);
        b_1_1->set_offset(0);
        b_1_1->set_size(5);

        auto b_1_2 = pr_file_1.add_blocks();
        b_1_2->set_hash(b2_hash);
        b_1_2->set_offset(5ul);
        b_1_2->set_size(5);

        proto::FileInfo pr_file_2;
        pr_file_2.set_name("b.txt");
        pr_file_2.set_block_size(5ul);
        pr_file_2.set_size(10ul);

        auto b_2_1 = pr_file_2.add_blocks();
        b_2_1->set_hash(b1_hash);
        b_2_1->set_offset(0);
        b_2_1->set_size(5);

        auto b_2_2 = pr_file_2.add_blocks();
        b_2_2->set_hash(b3_hash);
        b_2_2->set_offset(5ul);
        b_2_2->set_size(5);

        auto bi_1 = model::block_info_t::create(*b_1_1).value();
        auto bi_2 = model::block_info_t::create(*b_1_2).value();
        auto bi_3 = model::block_info_t::create(*b_2_2).value();

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
        CHECK(blocks.contains(bi_2->get_key()));

        orphans.record(*file_2);
        blocks = orphans.deduce();
        REQUIRE(blocks.size() == 3);
        CHECK(blocks.contains(bi_1->get_key()));
        CHECK(blocks.contains(bi_2->get_key()));
        CHECK(blocks.contains(bi_3->get_key()));
    }
}
