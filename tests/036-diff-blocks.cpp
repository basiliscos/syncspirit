// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "diff-builder.h"

#include "model/diff/modify/blocks_availability.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("various block diffs", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);

    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = cluster->get_folders().by_id("1234-5678");
    auto folder_info = folder->get_folder_infos().by_device(*my_device);

    auto b1_hash = utils::sha256_digest("12345").value();
    auto b2_hash = utils::sha256_digest("567890").value();

    proto::FileInfo pr_file_info;
    pr_file_info.set_name("a.txt");
    pr_file_info.set_block_size(5ul);
    pr_file_info.set_size(10ul);
    auto b1 = pr_file_info.add_blocks();
    b1->set_hash(b1_hash);
    b1->set_offset(0);
    b1->set_size(5);

    auto b2 = pr_file_info.add_blocks();
    b2->set_hash(b2_hash);
    b2->set_offset(5ul);
    b2->set_size(5);

    REQUIRE(builder.local_update(folder->get_id(), pr_file_info).apply());

    auto file = folder_info->get_file_infos().by_name("a.txt");
    auto bi1 = cluster->get_blocks().get(b1_hash);
    auto bi2 = cluster->get_blocks().get(b2_hash);
    file->remove_blocks();
    file->assign_block(bi1, 0);
    file->assign_block(bi2, 1);
    REQUIRE(!file->is_locally_available());

    auto callback = [&](diff::modify::block_transaction_t &diff) {
        REQUIRE(diff.errors.load() == 0);
        builder.ack_block(diff);
    };

    SECTION("append") {
        REQUIRE(builder.append_block(*file, 0, "12345", callback).apply());
        auto &blocks = file->get_blocks();

        auto lf1 = blocks[0]->local_file();
        REQUIRE(lf1);
        CHECK(lf1.block_index() == 0);
        CHECK(lf1.get_offset() == 0);
        CHECK(lf1.is_locally_available());
        CHECK(!file->is_locally_available());
    }

    SECTION("clone, from different file") {
        proto::FileInfo pr_source;
        pr_source.set_name("b.txt");
        pr_source.set_block_size(5ul);
        pr_source.set_size(5ul);
        auto b1 = pr_source.add_blocks();
        b1->set_hash(b2_hash);
        b1->set_offset(0);
        b1->set_size(5);

        REQUIRE(builder.local_update(folder->get_id(), pr_source).apply());
        auto source = folder_info->get_file_infos().by_name("b.txt");
        auto b2 = source->get_blocks().at(0);
        b2->mark_local_available(source.get());

        auto fb = model::file_block_t(bi2.get(), file.get(), 1);
        REQUIRE(builder.clone_block(fb, callback).apply());

        auto &blocks = file->get_blocks();
        auto lf1 = blocks[1]->local_file();
        REQUIRE(lf1);
        CHECK(lf1.block_index() == 1);
        CHECK(lf1.get_offset() == 5);
        CHECK(lf1.is_locally_available());
        CHECK(!file->is_locally_available());
    }

    SECTION("availability") {
        auto bdiff = diff::block_diff_ptr_t(new diff::modify::blocks_availability_t(*file, 1));
        REQUIRE(bdiff->apply(*cluster));
        CHECK(file->is_locally_available());
    }
}
