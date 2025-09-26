// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
#include "model/cluster.h"
#include "model/misc/sequencer.h"
#include "model/diff/modify/add_blocks.h"

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

    auto expected_ec = make_error_code(error_code_t::mismatch_file_size);
    REQUIRE(ec.value() == expected_ec.value());
    REQUIRE(ec.message() == expected_ec.message());
};

TEST_CASE("folder update (Index), ignore blocks on non-files", "[model]") {
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
    proto::set_name(pr_fi, "some-dir");
    proto::set_type(pr_fi, FileInfoType::DIRECTORY);
    proto::set_block_size(pr_fi, 5ul);
    proto::set_size(pr_fi, 0);
    proto::set_sequence(pr_fi, 5);

    auto b1_hash = utils::sha256_digest(as_bytes("12345")).value();
    auto &b1 = proto::add_blocks(pr_fi);
    proto::set_hash(b1, as_bytes("12345"));
    proto::set_size(b1, 5);

    REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_fi, peer_device).finish().apply());

    auto &folders = cluster->get_folders();
    auto folder = folders.by_id("1234-5678");
    auto folder_peer = folder->get_folder_infos().by_device(*peer_device);
    CHECK(folder_peer->get_file_infos().size() == 1);
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
        auto b_data = as_owned_bytes("123");
        proto::set_hash(b, utils::sha256_digest(b_data).value());
        proto::set_size(b, 5);

        auto b2_data = as_owned_bytes("345");
        auto b2_hash = utils::sha256_digest(b2_data).value();

        auto &blocks_map = cluster->get_blocks();

        SECTION("invalid cases: already known") {
            auto ec = builder.make_index(sha256, "1234-5678").add(pr_file, peer_device, false).fail();
            REQUIRE(ec);

            auto expected_ec = make_error_code(error_code_t::missing_version);
            REQUIRE(ec.value() == expected_ec.value());
            REQUIRE(ec.message() == expected_ec.message());
        }

        SECTION("invalid cases: already known files") {
            REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).finish().apply());

            SECTION("different filename") {
                proto::set_name(pr_file, "b.txt");
                auto ec = builder.make_index(sha256, "1234-5678").add(pr_file, peer_device, true).fail();
                auto expected_ec = make_error_code(error_code_t::invalid_sequence);
                REQUIRE(ec.value() == expected_ec.value());
            }
            SECTION("different content") {
                proto::set_size(pr_file, 10ul);

                auto &b2 = proto::add_blocks(pr_file);
                proto::set_hash(b2, utils::sha256_digest(b_data).value());
                proto::set_size(b2, 5);
                proto::set_offset(b2, 5);

                auto ec = builder.make_index(sha256, "1234-5678").add(pr_file, peer_device, true).fail();
                auto expected_ec = make_error_code(error_code_t::invalid_sequence);
                REQUIRE(ec.value() == expected_ec.value());
            }
        }

        SECTION("valid cases") {
            REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).finish().apply());

            auto &peer_files = peer_folder_info->get_file_infos();
            REQUIRE(peer_files.size() == 1);
            auto f = peer_files.by_name("a.txt");
            REQUIRE(f);

            SECTION("exactly the same file is added, and it's duplicate is ignored") {
                REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).finish().apply());
                REQUIRE(peer_files.size() == 1);
            }

            SECTION("when a file with existing name is added, key & instance are kept") {
                proto::set_sequence(pr_file, 11ul);
                proto::set_modified_s(pr_file, 2);
                REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).finish().apply());

                REQUIRE(peer_files.size() == 1);
                auto same_f = peer_files.by_name(proto::get_name(pr_file));
                CHECK(same_f.get() == f.get());
                CHECK(same_f->get_full_id() == f->get_full_id());
            }

            SECTION("file with new blocks is added, the preivous one is removed") {
                REQUIRE(blocks_map.size() == 1);
                auto prev_block = *blocks_map.begin();
                proto::set_sequence(pr_file, 11ul);
                proto::set_modified_s(pr_file, 2);
                proto::set_hash(b, b2_hash);
                REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).finish().apply());

                REQUIRE(peer_files.size() == 1);
                REQUIRE(blocks_map.size() == 1);
                auto new_block = *blocks_map.begin();
                CHECK(new_block != prev_block);
                CHECK(new_block->get_hash() != prev_block->get_hash());
            }
        }
    }
    SECTION("folder does not exists") {
        auto ec = builder.make_index(sha256, "1234-5678-xxx").fail();
        REQUIRE(ec);
        auto expected_ec = make_error_code(error_code_t::folder_does_not_exist);
        REQUIRE(ec.value() == expected_ec.value());
        REQUIRE(ec.message() == expected_ec.message());
    }

    SECTION("folder is not shared") {
        auto ec = builder.make_index(sha256, "5555-4444").fail();
        REQUIRE(ec);
        auto expected_ec = make_error_code(error_code_t::folder_is_not_shared);
        REQUIRE(ec.value() == expected_ec.value());
        REQUIRE(ec.message() == expected_ec.message());
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

        REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).finish().apply());
    }

    SECTION("non-files with blocks are ignored") {
        auto pr_file = proto::FileInfo();
        proto::set_name(pr_file, "a.txt");
        proto::set_type(pr_file, FileInfoType::SYMLINK);
        proto::set_block_size(pr_file, 5ul);
        proto::set_size(pr_file, 5ul);
        proto::set_sequence(pr_file, 10ul);
        proto::set_modified_s(pr_file, 1);

        auto &b = proto::add_blocks(pr_file);
        proto::set_hash(b, as_bytes("123"));
        proto::set_size(b, 5);

        REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).finish().apply());

        auto peer_folder = folder->get_folder_infos().by_device(*peer_device);
        auto &peer_files = peer_folder->get_file_infos();
        REQUIRE(peer_files.size() == 1);

        auto file = peer_files.by_name("a.txt");
        REQUIRE(file);
        CHECK(file->get_size() == 0);
    }
}

TEST_CASE("move block", "[model]") {
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

    auto pr_file_1 = proto::FileInfo();
    proto::set_name(pr_file_1, "a.txt");
    proto::set_block_size(pr_file_1, 5ul);
    proto::set_size(pr_file_1, 5ul);
    proto::set_sequence(pr_file_1, 10ul);

    auto b_1 = proto::BlockInfo();
    proto::set_hash(b_1, as_bytes("12345"));
    proto::set_size(b_1, 5);

    auto b_2 = proto::BlockInfo();
    proto::set_hash(b_2, as_bytes("67890"));
    proto::set_size(b_2, 5);

    proto::add_blocks(pr_file_1) = b_1;

    REQUIRE(builder.make_index(sha256, "1234-5678").add(pr_file_1, peer_device).finish().apply());
    CHECK(cluster->get_blocks().size() == 1);

    proto::set_sequence(pr_file_1, 11ul);
    proto::set_blocks(pr_file_1, 0, b_2);

    auto pr_file_2 = proto::FileInfo();
    proto::set_name(pr_file_2, "b.txt");
    proto::set_block_size(pr_file_2, 5ul);
    proto::set_size(pr_file_2, 5ul);
    proto::set_sequence(pr_file_2, 12ul);
    proto::add_blocks(pr_file_2) = b_1;
    REQUIRE(builder.make_index(sha256, "1234-5678")
                .add(pr_file_1, peer_device)
                .add(pr_file_2, peer_device)
                .finish()
                .apply());
    CHECK(cluster->get_blocks().size() == 2);
}

TEST_CASE("duplicate blocks", "[model]") {
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

    auto pr_file = proto::FileInfo();
    proto::set_name(pr_file, "a.txt");
    proto::set_block_size(pr_file, 5ul);
    proto::set_size(pr_file, 10ul);
    proto::set_sequence(pr_file, 10ul);

    auto b_1 = proto::BlockInfo();
    proto::set_hash(b_1, as_bytes("12345"));
    proto::set_size(b_1, 5);

    auto b_2 = proto::BlockInfo();
    proto::set_hash(b_2, as_bytes("12345"));
    proto::set_size(b_2, 5);
    proto::set_offset(b_2, 5);

    proto::add_blocks(pr_file) = b_1;
    proto::add_blocks(pr_file) = b_2;

    auto diff = builder.make_index(sha256, "1234-5678").add(pr_file, peer_device).finish().extract();
    auto child_diff = diff->child;
    REQUIRE(child_diff);

    auto add_blocks_diff = dynamic_cast<model::diff::modify::add_blocks_t *>(child_diff.get());
    REQUIRE(add_blocks_diff);
    REQUIRE(add_blocks_diff->blocks.size() == 1);
}

TEST_CASE("update folder-3: duplicate files", "[model]") {
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

    auto folder_peer = folder->get_folder_infos().by_device(*peer_device);

    auto pr_file = proto::FileInfo();
    proto::set_name(pr_file, "a.txt");
    proto::set_block_size(pr_file, 5ul);
    proto::set_size(pr_file, 5ul);
    proto::set_sequence(pr_file, 10ul);
    proto::set_modified_s(pr_file, 1);

    auto &b = proto::add_blocks(pr_file);
    auto b_data = as_owned_bytes("123");
    proto::set_hash(b, utils::sha256_digest(b_data).value());
    proto::set_size(b, 5);

    auto &v1 = proto::get_version(pr_file);
    auto c1 = proto::add_counters(v1);
    proto::set_id(c1, peer_device->device_id().get_uint());
    proto::set_value(c1, 2);

    auto b2_data = as_owned_bytes("345");
    auto b2_hash = utils::sha256_digest(b2_data).value();

    auto &blocks_map = cluster->get_blocks();

    auto pr_file_2 = pr_file;
    proto::set_sequence(pr_file_2, 11ul);
    proto::set_size(pr_file_2, 10l);

    auto &b2 = proto::add_blocks(pr_file_2);
    proto::set_hash(b2, utils::sha256_digest(b2_data).value());
    proto::set_size(b2, 5);
    proto::set_offset(b2, 5);

    auto c2 = c1;
    proto::set_value(c2, 3);
    auto &v2 = proto::get_version(pr_file_2);
    proto::add_counters(v2, c2);

    SECTION("the same file, updated, in same index") {
        REQUIRE(builder.make_index(sha256, "1234-5678")
                    .add(pr_file, peer_device, false)
                    .add(pr_file_2, peer_device, false)
                    .finish()
                    .apply());

        CHECK(blocks_map.size() == 2);
        CHECK(folder_peer->get_max_sequence() == 11ul);
        CHECK(folder_peer->get_file_infos().size() == 1);
    }
    SECTION("2 diffs with the exacly same file") {
        blocks_map.clear();
        REQUIRE(builder.unshare_folder(*folder_peer).then().share_folder(peer_id.get_sha256(), "1234-5678").apply());
        // clang-format off
        REQUIRE(builder
                .make_index(sha256, "1234-5678").add(pr_file, peer_device, false).finish()
                .make_index(sha256, "1234-5678").add(pr_file, peer_device, false).finish()
            .apply());
        // clang-format on

        folder_peer = folder->get_folder_infos().by_device(*peer_device);

        CHECK(blocks_map.size() == 1);
        CHECK(folder_peer->get_max_sequence() == 10ul);
        CHECK(folder_peer->get_file_infos().size() == 1);
    }
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
