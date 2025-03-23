// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
#include "model/cluster.h"
#include "model/misc/sequencer.h"

#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("folder update (Index)", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto index_id = uint64_t{0x1234};

    auto max_seq = int64_t(10);
    auto builder = diff_builder_t(*cluster);
    auto sha256 = peer_id.get_sha256();
    REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
    REQUIRE(builder.share_folder(sha256, "1234-5678").apply());
    REQUIRE(builder.configure_cluster(sha256).add(sha256, "1234-5678", index_id, max_seq).finish().apply());

    proto::FileInfo pr_fi;
    proto::set_name(pr_fi, "a.txt");
    proto::set_block_size(pr_fi, 5ul);
    proto::set_size(pr_fi, 6);
    proto::set_sequence(pr_fi, 5);

    auto b1_hash = utils::sha256_digest(as_bytes("12345")).value();
    auto &b1 = proto::add_blocks(pr_fi);
    proto::set_hash(b1, as_bytes("12345"));
    proto::set_size(b1, 5);

    auto ec = builder.make_index(sha256, "1234-5678").add(pr_fi, peer_device).fail();
    REQUIRE(ec);
    CHECK(ec == make_error_code(error_code_t::mismatch_file_size));
};

TEST_CASE("update folder-2 (via Index)", "[model]") {
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
    proto::set_folder(pr_index, "1234-5678");

    auto sha256 = peer_id.get_sha256();
    SECTION("successful case") {
        auto peer_folder_info = folder->get_folder_infos().by_device(*peer_device);

        auto pr_file = proto::FileInfo();
        proto::set_name(pr_file, "a.txt");
        proto::set_block_size(pr_file, 5ul);
        proto::set_size(pr_file, 5ul);
        proto::set_sequence(pr_file, 10ul);
        proto::set_modified_s(pr_file, 1);

        auto &b = proto::add_blocks(pr_file);
        proto::set_hash(b, as_bytes("123"));
        proto::set_size(b, 5);

        SECTION("invalid cases") {
            auto ec = builder.make_index(sha256, "1234-5678").add(pr_file, peer_device, false).fail();
            REQUIRE(ec);
            CHECK(ec == model::make_error_code(model::error_code_t::missing_version));
        }

        SECTION("valid cases") {
            REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).finish().apply());

            auto &peer_files = peer_folder_info->get_file_infos();
            REQUIRE(peer_files.size() == 1);
            auto f = peer_files.by_name("a.txt");
            REQUIRE(f);

            auto key = f->get_key();

            SECTION("when a file with existing name is added, key & instance are kept") {
                proto::set_sequence(pr_file, 11ul);
                proto::set_modified_s(pr_file, 2);
                REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).finish().apply());

                REQUIRE(peer_files.size() == 1);
                auto same_f = peer_files.by_name(proto::get_name(pr_file));
                CHECK(same_f.get() == f.get());
                CHECK(same_f->get_key() == f->get_key());
            }

            SECTION("file with new blocks is added, the preivous one is removed") {
                auto &blocks_map = cluster->get_blocks();
                REQUIRE(blocks_map.size() == 1);
                auto prev_block = blocks_map.begin()->item;
                proto::set_sequence(pr_file, 11ul);
                proto::set_modified_s(pr_file, 2);
                proto::set_hash(b, as_bytes("345"));
                REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).finish().apply());

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
        auto pr_file = proto::FileInfo();
        proto::set_name(pr_file, "a.txt");
        proto::set_block_size(pr_file, 5ul);
        proto::set_size(pr_file, 5ul);
        proto::set_sequence(pr_file, 10ul);
        proto::set_modified_s(pr_file, 1);
        proto::set_deleted(pr_file, true);

        auto &b = proto::add_blocks(pr_file);
        proto::set_hash(b, as_bytes("123"));
        proto::set_size(b, 5);

        auto ec = builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).fail();
        REQUIRE(ec);
        CHECK(ec == model::make_error_code(model::error_code_t::unexpected_blocks));
    }
}
