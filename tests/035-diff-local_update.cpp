// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/local_update.h"
#include "model/diff/modify/new_file.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("local_update diff", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    auto &blocks_map = cluster->get_blocks();

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
    REQUIRE(diff->apply(*cluster));

    auto folder_info = cluster->get_folders().by_id(db_folder.id())->get_folder_infos().by_device(my_device);
    proto::FileInfo pr_file_info;
    pr_file_info.set_name("a.txt");

    SECTION("empty file -> file with blocks") {
        pr_file_info.set_type(proto::FileInfoType::FILE);
        pr_file_info.set_size(5ul);
        pr_file_info.set_block_size(5ul);
        diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_file_info, {}));
        REQUIRE(diff->apply(*cluster));

        auto file_info = folder_info->get_file_infos().by_name("a.txt");
        REQUIRE(file_info);

        db::FileInfo db_file;
        db_file.set_name("a.txt");
        db_file.set_size(5ul);
        db_file.set_block_size(5ul);
        db_file.set_type(pr_file_info.type());

        auto bi = proto::BlockInfo();
        bi.set_size(5);
        bi.set_weak_hash(12);
        bi.set_hash(utils::sha256_digest("12345").value());
        bi.set_offset(0);

        diff = diff::cluster_diff_ptr_t(new diff::modify::local_update_t(*file_info, file_info->as_db(false), {bi}));
        REQUIRE(diff->apply(*cluster));
        CHECK(file_info->get_sequence() == 2ul);
        CHECK(file_info->get_name() == pr_file_info.name());

        auto &blocks = file_info->get_blocks();
        REQUIRE(blocks.size() == 1);
        REQUIRE(blocks[0]->get_hash() == bi.hash());

        REQUIRE(blocks_map.size() == 1);
        REQUIRE(blocks_map.get(bi.hash()));
    }

    SECTION("file with blocks -> empty") {
        pr_file_info.set_type(proto::FileInfoType::FILE);
        pr_file_info.set_size(5ul);
        pr_file_info.set_block_size(5ul);

        auto bi = proto::BlockInfo();
        bi.set_size(5);
        bi.set_weak_hash(12);
        bi.set_hash(utils::sha256_digest("12345").value());
        bi.set_offset(0);

        diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_file_info, {bi}));
        REQUIRE(diff->apply(*cluster));
        REQUIRE(blocks_map.size() == 1);

        auto file_info = folder_info->get_file_infos().by_name("a.txt");
        REQUIRE(file_info);

        db::FileInfo db_file;
        db_file.set_name("a.txt");
        db_file.set_size(0ul);
        db_file.set_type(pr_file_info.type());

        diff = diff::cluster_diff_ptr_t(new diff::modify::local_update_t(*file_info, db_file, {}));
        REQUIRE(diff->apply(*cluster));
        CHECK(file_info->get_sequence() == 2ul);

        auto &blocks = file_info->get_blocks();
        REQUIRE(blocks.size() == 0);

        REQUIRE(blocks_map.size() == 0);
    }
}
