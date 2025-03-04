// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "diff-builder.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("update folder (via Index)", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);

    REQUIRE(builder.upsert_folder("1234-5678", "/my/path").upsert_folder("5555-4444", "/p2").apply());
    auto folder = folders.by_id("1234-5678");
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());

    auto pr_index = proto::Index();
    pr_index.set_folder("1234-5678");

    auto sha256 = peer_id.get_sha256();
    SECTION("successful case") {
        auto peer_folder_info = folder->get_folder_infos().by_device(*peer_device);

        auto file = proto::FileInfo();
        file.set_name("a.txt");
        file.set_sequence(10ul);
        file.set_size(5ul);
        file.set_block_size(5ul);
        file.set_modified_s(1);
        auto b = file.add_blocks();
        b->set_hash("123");
        b->set_size(5ul);

        SECTION("invalid cases") {
            auto ec = builder.make_index(sha256, "1234-5678").add(file, peer_device, false).fail();
            REQUIRE(ec);
            CHECK(ec == model::make_error_code(model::error_code_t::missing_version));
        }

        SECTION("valid cases") {
            REQUIRE(builder.make_index(sha256, "1234-5678").add(file, peer_device).finish().apply());

            auto &peer_files = peer_folder_info->get_file_infos();
            REQUIRE(peer_files.size() == 1);
            auto f = peer_files.by_name("a.txt");
            REQUIRE(f);

            auto key = std::string(f->get_key());

            SECTION("when a file with existing name is added, key & instance are kept") {
                file.set_modified_s(2);
                file.set_sequence(11ul);
                REQUIRE(builder.make_index(sha256, "1234-5678").add(file, peer_device).finish().apply());

                REQUIRE(peer_files.size() == 1);
                auto same_f = peer_files.by_name(file.name());
                CHECK(same_f.get() == f.get());
                CHECK(same_f->get_key() == f->get_key());
            }

            SECTION("file with new blocks is added, the preivous one is removed") {
                auto &blocks_map = cluster->get_blocks();
                REQUIRE(blocks_map.size() == 1);
                auto prev_block = blocks_map.begin()->item;
                file.set_modified_s(2);
                file.set_sequence(11ul);
                file.mutable_blocks(0)->set_hash("345");
                REQUIRE(builder.make_index(sha256, "1234-5678").add(file, peer_device).finish().apply());

                REQUIRE(peer_files.size() == 1);
                REQUIRE(blocks_map.size() == 1);
                auto new_block = blocks_map.begin()->item;
                CHECK(new_block != prev_block);
                CHECK(new_block->get_hash() != prev_block->get_hash());
            }
        }
    }
    SECTION("folder does not exists") {
        auto ec = builder.make_index(sha256, "1234-5678-xxx").fail();
        REQUIRE(ec);
        CHECK(ec == model::make_error_code(model::error_code_t::folder_does_not_exist));
    }

    SECTION("folder is not shared") {
        auto ec = builder.make_index(sha256, "5555-4444").fail();
        REQUIRE(ec);
        CHECK(ec == model::make_error_code(model::error_code_t::folder_is_not_shared));
    }

    SECTION("blocks are not expected") {
        auto file = proto::FileInfo();
        file.set_name("a.txt");
        file.set_sequence(10ul);
        file.set_size(5ul);
        file.set_block_size(5ul);
        file.set_deleted(true);
        auto b = file.add_blocks();
        b->set_hash("123");

        auto ec = builder.make_index(sha256, "1234-5678").add(file, peer_device).fail();
        REQUIRE(ec);
        CHECK(ec == model::make_error_code(model::error_code_t::unexpected_blocks));
    }
}
