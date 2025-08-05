// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

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
                return block_iterator->next();
            }
        }
        return {};
    };

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto my_folder = folder_infos.by_device(*my_device);

    auto p_file = proto::FileInfo();
    proto::set_name(p_file, "a.txt");
    proto::set_sequence(p_file, 2);

    SECTION("no blocks") {
        REQUIRE(builder.local_update(folder->get_id(), p_file).apply());

        auto my_file = my_folder->get_file_infos().by_name(proto::get_name(p_file));
        REQUIRE(!next(my_file, true));
    }

    auto b1_hash = utils::sha256_digest(as_bytes("12345")).value();
    auto b2_hash = utils::sha256_digest(as_bytes("567890")).value();
    auto &b1 = proto::add_blocks(p_file);
    proto::set_hash(b1, b1_hash);
    proto::set_size(b1, 5);

    SECTION("two blocks") {
        auto b1 = proto::get_blocks(p_file, 0);
        auto &b2 = proto::add_blocks(p_file);
        proto::set_hash(b2, b2_hash);
        proto::set_size(b2, 5);
        proto::set_size(p_file, 10);
        proto::set_block_size(p_file, proto::get_size(b1));

        SECTION("no iteration upon deleted file") {
            proto::set_block_size(p_file, 0);
            proto::clear_blocks(p_file);
            proto::set_deleted(p_file, true);

            REQUIRE(builder.local_update(folder->get_id(), p_file).apply());

            auto my_file = my_folder->get_file_infos().by_name(proto::get_name(p_file));
            CHECK(!next(my_file, true));
        }

        SECTION("normal iteration") {
            REQUIRE(builder.local_update(folder->get_id(), p_file).apply());

            auto my_file = my_folder->get_file_infos().by_name(proto::get_name(p_file));
            auto bi1 = cluster->get_blocks().by_hash(b1_hash);
            auto bi2 = cluster->get_blocks().by_hash(b2_hash);
            my_file->remove_blocks();
            my_file->assign_block(bi1, 0);
            my_file->assign_block(bi2, 1);

            auto fb1 = next(my_file, true);
            REQUIRE(fb1);
            CHECK(fb1.block()->get_hash() == b1_hash);
            CHECK(fb1.block_index() == 0);
            CHECK(fb1.file() == my_file.get());

            auto fb2 = next(my_file);
            REQUIRE(fb2);
            CHECK(fb2.block()->get_hash() == b2_hash);
            CHECK(fb2.block_index() == 1);
            CHECK(fb2.file() == my_file.get());

            REQUIRE(!next(my_file));
        }

        SECTION("no iteration upon unavailable file") {
            REQUIRE(builder.local_update(folder->get_id(), p_file).apply());

            auto my_file = my_folder->get_file_infos().by_name(proto::get_name(p_file));
            auto bi1 = cluster->get_blocks().by_hash(b1_hash);
            auto bi2 = cluster->get_blocks().by_hash(b2_hash);
            my_file->remove_blocks();
            my_file->assign_block(bi1, 0);
            my_file->assign_block(bi2, 1);

            my_file->mark_unreachable(true);
            CHECK(!next(my_file, true));
        }
    }

    SECTION("block is available on some other local file") {
        auto p_file_2 = proto::FileInfo();
        proto::set_name(p_file_2, "b.txt");
        proto::set_sequence(p_file_2, 2);
        proto::set_size(p_file_2, proto::get_size(b1));
        proto::set_block_size(p_file_2, proto::get_size(b1));
        proto::add_blocks(p_file_2, b1);

        proto::set_size(p_file, proto::get_size(b1));

        REQUIRE(builder.local_update(folder->get_id(), p_file_2).apply());

        auto my_file_2 = my_folder->get_file_infos().by_name(proto::get_name(p_file_2));
        REQUIRE(my_file_2->is_locally_available());

        REQUIRE(builder.local_update(folder->get_id(), p_file).apply());

        auto my_file = my_folder->get_file_infos().by_name(proto::get_name(p_file));
        auto bi1 = cluster->get_blocks().by_hash(b1_hash);
        my_file->remove_blocks();
        my_file->assign_block(bi1, 0);
        REQUIRE(!my_file->is_locally_available());

        auto fb1 = next(my_file, true);
        REQUIRE(fb1);
        CHECK(fb1.block()->get_hash() == b1_hash);
        CHECK(fb1.block_index() == 0);
        CHECK(fb1.file() == my_file.get());
    }
}
