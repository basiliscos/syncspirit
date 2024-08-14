// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "model/cluster.h"
#include "model/misc/block_iterator.h"
#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

TEST_CASE("block iterator", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
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
    auto builder = diff_builder_t(*cluster);
    builder.create_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto my_folder = folder_infos.by_device(*my_device);

    auto p_file = proto::FileInfo();
    p_file.set_name("a.txt");
    p_file.set_sequence(2ul);

    SECTION("no blocks") {
        REQUIRE(builder.local_update(folder->get_id(), p_file).apply());

        auto my_file = my_folder->get_file_infos().by_name(p_file.name());
        REQUIRE(!next(my_file, true));
    }

    auto b1_hash = utils::sha256_digest("12345").value();
    auto b2_hash = utils::sha256_digest("567890").value();
    auto b1 = p_file.add_blocks();
    b1->set_size(5);
    b1->set_weak_hash(12);
    b1->set_hash(b1_hash);

    SECTION("two blocks") {
        auto b2 = p_file.add_blocks();
        b2->set_size(5);
        b2->set_weak_hash(12);
        b2->set_hash(b2_hash);

        p_file.set_size(10ul);
        p_file.set_block_size(5ul);

        SECTION("no iteration upon deleted file") {
            p_file.set_deleted(true);

            REQUIRE(builder.local_update(folder->get_id(), p_file).apply());

            auto my_file = my_folder->get_file_infos().by_name(p_file.name());
            CHECK(!next(my_file, true));
        }

        SECTION("normal iteration") {
            REQUIRE(builder.local_update(folder->get_id(), p_file).apply());

            auto my_file = my_folder->get_file_infos().by_name(p_file.name());
            auto bi1 = cluster->get_blocks().get(b1_hash);
            auto bi2 = cluster->get_blocks().get(b2_hash);
            my_file->remove_blocks();
            my_file->assign_block(bi1, 0);
            my_file->assign_block(bi2, 1);

            auto fb1 = next(my_file, true);
            REQUIRE(fb1);
            CHECK(fb1.block()->get_hash() == b1_hash);
            CHECK(fb1.block_index() == 0);
            CHECK(fb1.file() == my_file.get());

            auto fb1_1 = next(my_file);
            REQUIRE(fb1_1);
            CHECK(fb1_1.block()->get_hash() == b1_hash);
            CHECK(fb1_1.block_index() == 0);
            CHECK(fb1_1.file() == my_file.get());

            auto fb2 = next(my_file);
            REQUIRE(fb2);
            CHECK(fb2.block()->get_hash() == b2_hash);
            CHECK(fb2.block_index() == 1);
            CHECK(fb2.file() == my_file.get());

            REQUIRE(!next(my_file));
        }

        SECTION("no iteration upon unavailable file") {
            REQUIRE(builder.local_update(folder->get_id(), p_file).apply());

            auto my_file = my_folder->get_file_infos().by_name(p_file.name());
            auto bi1 = cluster->get_blocks().get(b1_hash);
            auto bi2 = cluster->get_blocks().get(b2_hash);
            my_file->remove_blocks();
            my_file->assign_block(bi1, 0);
            my_file->assign_block(bi2, 1);

            my_file->mark_unreachable(true);
            CHECK(!next(my_file, true));
        }
    }

    SECTION("block is available on some other local file") {
        auto p_file_2 = proto::FileInfo();
        p_file_2.set_name("b.txt");
        p_file_2.set_sequence(2ul);
        p_file_2.set_size(b1->size());
        *p_file_2.add_blocks() = *b1;
        p_file.set_size(b1->size());

        REQUIRE(builder.local_update(folder->get_id(), p_file_2).apply());

        auto my_file_2 = my_folder->get_file_infos().by_name(p_file_2.name());
        REQUIRE(my_file_2->is_locally_available());

        REQUIRE(builder.local_update(folder->get_id(), p_file).apply());

        auto my_file = my_folder->get_file_infos().by_name(p_file.name());
        auto bi1 = cluster->get_blocks().get(b1_hash);
        my_file->remove_blocks();
        my_file->assign_block(bi1, 0);
        REQUIRE(!my_file->is_locally_available());

        auto fb1 = next(my_file, true);
        REQUIRE(fb1);
        CHECK(fb1.block()->get_hash() == b1_hash);
        CHECK(fb1.block_index() == 0);
        CHECK(fb1.file() == my_file.get());
    }

    SECTION("locked/unlock blocks") {
        p_file.set_size(5ul);
        p_file.set_block_size(5ul);

        REQUIRE(builder.local_update(folder->get_id(), p_file).apply());
        auto my_file = my_folder->get_file_infos().by_name(p_file.name());

        auto bi1 = cluster->get_blocks().get(b1_hash);
        my_file->remove_blocks();
        my_file->assign_block(bi1, 0);
        REQUIRE(!my_file->is_locally_available());

        auto fb = next(my_file, true);
        REQUIRE(fb);
        auto block = fb.block();

        block->lock();
        REQUIRE(!next(my_file, true));

        block->unlock();
        REQUIRE(next(my_file, true));
    }
}
