// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "catch.hpp"
#include "test-utils.h"
#include "model/cluster.h"
#include "model/misc/block_iterator.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/new_file.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/peer/update_folder.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

TEST_CASE("block iterator", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);

    auto block_iterator = block_iterator_ptr_t();
    auto next = [&](file_info_ptr_t source, bool reset = false) -> file_block_t {
        if (source && source->is_file() && !source->is_deleted()) {
            if (reset) {
                block_iterator = new blocks_iterator_t(*source);
            }
            if (block_iterator && *block_iterator) {
                return block_iterator->next(!reset);
            }
        }
        return {};
    };

    auto &folders = cluster->get_folders();
    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");

    auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
    REQUIRE(diff->apply(*cluster));

    auto folder = folders.by_id(db_folder.id());
    auto &folder_infos = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
    auto my_folder = folder_infos.by_device(my_device);

    auto pr_index = proto::Index();
    pr_index.set_folder(db_folder.id());

    auto p_file = pr_index.add_files();
    p_file->set_name("a.txt");
    p_file->set_sequence(2ul);

    SECTION("no blocks") {
        diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), *p_file, {}));
        REQUIRE(diff->apply(*cluster));

        auto my_file = my_folder->get_file_infos().by_name(p_file->name());
        REQUIRE(!next(my_file, true));
    }

    auto bi1 = proto::BlockInfo();
    bi1.set_size(5);
    bi1.set_weak_hash(12);
    bi1.set_hash(utils::sha256_digest("12345").value());
    bi1.set_offset(0);

    SECTION("two blocks") {
        auto bi2 = proto::BlockInfo();
        bi2.set_size(5);
        bi2.set_weak_hash(12);
        bi2.set_hash(utils::sha256_digest("567890").value());
        bi2.set_offset(5ul);

        p_file->set_size(10ul);
        p_file->set_block_size(5ul);
        *p_file->add_blocks() = bi1;
        *p_file->add_blocks() = bi2;

        SECTION("no iteration upon deleted file") {
            p_file->set_deleted(true);

            diff = new diff::modify::new_file_t(*cluster, db_folder.id(), *p_file, {bi1, bi2});
            REQUIRE(diff->apply(*cluster));

            auto my_file = my_folder->get_file_infos().by_name(p_file->name());
            CHECK(!next(my_file, true));
        }

        SECTION("normal iteration") {
            diff =
                diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), *p_file, {bi1, bi2}));
            REQUIRE(diff->apply(*cluster));

            auto my_file = my_folder->get_file_infos().by_name(p_file->name());

            auto fb1 = next(my_file, true);
            REQUIRE(fb1);
            CHECK(fb1.block()->get_hash() == bi1.hash());
            CHECK(fb1.block_index() == 0);
            CHECK(fb1.file() == my_file.get());

            auto fb1_1 = next(my_file);
            REQUIRE(fb1_1);
            CHECK(fb1_1.block()->get_hash() == bi1.hash());
            CHECK(fb1_1.block_index() == 0);
            CHECK(fb1_1.file() == my_file.get());

            auto fb2 = next(my_file);
            REQUIRE(fb2);
            CHECK(fb2.block()->get_hash() == bi2.hash());
            CHECK(fb2.block_index() == 1);
            CHECK(fb2.file() == my_file.get());

            REQUIRE(!next(my_file));
        }
    }

    SECTION("block is available on some other local file") {
        auto p_file_2 = pr_index.add_files();
        p_file_2->set_name("b.txt");
        p_file_2->set_sequence(2ul);
        *p_file_2->add_blocks() = bi1;
        *p_file->add_blocks() = bi1;

        diff = new diff::modify::new_file_t(*cluster, db_folder.id(), *p_file_2, {bi1});
        REQUIRE(diff->apply(*cluster));

        auto my_file_2 = my_folder->get_file_infos().by_name(p_file_2->name());
        my_file_2->mark_local_available(0);
        REQUIRE(my_file_2->is_locally_available());

        diff = new diff::modify::new_file_t(*cluster, db_folder.id(), *p_file, {bi1});
        REQUIRE(diff->apply(*cluster));
        auto my_file = my_folder->get_file_infos().by_name(p_file->name());
        REQUIRE(!my_file->is_locally_available());

        auto fb1 = next(my_file, true);
        REQUIRE(fb1);
        CHECK(fb1.block()->get_hash() == bi1.hash());
        CHECK(fb1.block_index() == 0);
        CHECK(fb1.file() == my_file.get());
    }

    SECTION("locked/unlock blocks") {
        p_file->set_size(5ul);
        p_file->set_block_size(5ul);
        *p_file->add_blocks() = bi1;

        diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), *p_file, {bi1}));
        REQUIRE(diff->apply(*cluster));
        auto my_file = my_folder->get_file_infos().by_name(p_file->name());

        auto fb = next(my_file, true);
        REQUIRE(fb);
        auto block = fb.block();

        block->lock();
        REQUIRE(!next(my_file, true));

        block->unlock();
        REQUIRE(next(my_file, true));
    }
}
