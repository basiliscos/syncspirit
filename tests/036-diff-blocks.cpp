// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "diff-builder.h"

#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/local/blocks_availability.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("various block diffs", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    cluster->get_devices().put(my_device);

    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = cluster->get_folders().by_id("1234-5678");
    auto folder_info = folder->get_folder_infos().by_device(*my_device);

    auto b1_hash = utils::sha256_digest(as_bytes("12345")).value();
    auto b2_hash = utils::sha256_digest(as_bytes("567890")).value();

    proto::FileInfo pr_file;
    proto::set_name(pr_file, "a.txt");
    proto::set_block_size(pr_file, 5ul);
    proto::set_size(pr_file, 10ul);

    auto &b1 = proto::add_blocks(pr_file);
    proto::set_offset(b1, 0);
    proto::set_size(b1, 5);
    proto::set_hash(b1, b1_hash);

    auto &b2 = proto::add_blocks(pr_file);
    proto::set_offset(b2, 5);
    proto::set_size(b2, 5);
    proto::set_hash(b2, b2_hash);

    REQUIRE(builder.local_update(folder->get_id(), pr_file).apply());

    auto file = folder_info->get_file_infos().by_name("a.txt");
    auto bi1 = cluster->get_blocks().by_hash(b1_hash);
    auto bi2 = cluster->get_blocks().by_hash(b2_hash);
    file->remove_blocks();
    file->assign_block(bi1, 0);
    file->assign_block(bi2, 1);
    REQUIRE(!file->is_locally_available());

    SECTION("append") {
        auto diff_raw = new model::diff::modify::append_block_t(*file, *folder_info, 0, as_owned_bytes("12345"));
        auto diff = diff::cluster_diff_ptr_t(diff_raw);
        diff->assign_sibling(diff_raw->ack().get());
        REQUIRE(builder.assign(diff.get()).apply());
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
        proto::set_name(pr_source, "b.txt");
        proto::set_block_size(pr_source, 5ul);
        proto::set_size(pr_source, 5ul);

        auto &b1 = proto::add_blocks(pr_source);
        proto::set_offset(b1, 0);
        proto::set_size(b1, 5);
        proto::set_hash(b1, b2_hash);

        REQUIRE(builder.local_update(folder->get_id(), pr_source).apply());
        auto source = folder_info->get_file_infos().by_name("b.txt");
        auto b2 = source->get_blocks().at(0);
        b2->mark_local_available(source.get());

        auto fb = model::file_block_t(bi2.get(), file.get(), 1);
        auto diff_raw = new model::diff::modify::clone_block_t(fb, *folder_info, *folder_info);
        auto diff = diff::cluster_diff_ptr_t(diff_raw);
        diff->assign_sibling(diff_raw->ack().get());
        REQUIRE(builder.assign(diff.get()).apply());

        auto &blocks = file->get_blocks();
        auto lf1 = blocks[1]->local_file();
        REQUIRE(lf1);
        CHECK(lf1.block_index() == 1);
        CHECK(lf1.get_offset() == 5);
        CHECK(lf1.is_locally_available());
        CHECK(!file->is_locally_available());
    }

    SECTION("availability") {
        using blocks_map_t = diff::local::blocks_availability_t::valid_blocks_map_t;
        auto map = blocks_map_t(2);
        map[0] = map[1] = true;
        auto diff = diff::cluster_diff_ptr_t(new diff::local::blocks_availability_t(*file, *folder_info, map));
        REQUIRE(diff->apply(*controller, {}));
        CHECK(file->is_locally_available());
    }
}
