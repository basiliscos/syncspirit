// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("new file diff", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = cluster->get_folders().by_id("1234-5678");

    proto::FileInfo pr_file;
    proto::set_name(pr_file, "a.txt");

    SECTION("symlink, inc sequence, no blocks") {
        proto::set_type(pr_file, proto::FileInfoType::SYMLINK);
        proto::set_symlink_target(pr_file, "/some/where");
        REQUIRE(builder.local_update(folder->get_id(), pr_file).apply());

        auto folder_info = folder->get_folder_infos().by_device(*my_device);
        auto &files = folder_info->get_file_infos();
        auto file = files.by_name(proto::get_name(pr_file));
        REQUIRE(file);
        REQUIRE(file->get_name()->get_full_name() == "a.txt");
        REQUIRE(file->get_link_target() == "/some/where");
        REQUIRE(file->is_link());
        REQUIRE(file->get_sequence() == 1);
        REQUIRE(folder_info->get_max_sequence() == 1);
        REQUIRE(file->get_version().counters_size() == 1);
        REQUIRE(file->get_modified_by() == my_device->device_id().get_uint());
        auto &counter = file->get_version().get_best();
        auto v1 = proto::get_value(counter);
        CHECK(v1 > 0);

        SECTION("peer update") {
            file->get_version().update(*peer_device);
            auto p = file->get_version().as_proto();
            REQUIRE(proto::get_counters_size(p) == 2);

            auto &c0 = proto::get_counters(p, 0);
            CHECK(proto::get_value(c0) == v1);
            CHECK(proto::get_id(c0) == my_device->device_id().get_uint());

            auto &c1 = proto::get_counters(p, 1);
            CHECK(proto::get_value(c1) > v1);
            CHECK(proto::get_id(c1) == peer_device->device_id().get_uint());
        }

        SECTION("update it") {
            proto::set_symlink_target(pr_file, "/new/location");
            REQUIRE(builder.local_update(folder->get_id(), pr_file).apply());
            REQUIRE(files.size() == 1);

            auto new_file = files.by_name(file->get_name()->get_full_name());
            REQUIRE(new_file);
            CHECK(new_file.get() == file.get());
            CHECK(new_file->get_key() == file->get_key());
            REQUIRE(new_file->get_version().counters_size() == 1);
            auto v2 = proto::get_value(new_file->get_version().get_best());
            REQUIRE(v1 < v2);
        }
    }

    SECTION("file, no inc, new block") {
        proto::set_type(pr_file, proto::FileInfoType::FILE);
        proto::set_size(pr_file, 5ul);
        proto::set_block_size(pr_file, 5ul);

        auto hash = utils::sha256_digest(as_bytes("12345")).value();
        auto &pr_block = proto::add_blocks(pr_file);
        proto::set_size(pr_block, 5);
        proto::set_hash(pr_block, hash);

        REQUIRE(builder.local_update(folder->get_id(), pr_file).apply());

        auto folder_info = folder->get_folder_infos().by_device(*my_device);
        auto file = folder_info->get_file_infos().by_name(proto::get_name(pr_file));
        REQUIRE(file);
        REQUIRE(file->get_size() == 5);
        REQUIRE(file->get_name()->get_full_name() == "a.txt");
        REQUIRE(!file->is_link());
        REQUIRE(file->get_sequence() == 1);
        REQUIRE(folder_info->get_max_sequence() == 1);
        REQUIRE(file->get_blocks().size() == 1);
        REQUIRE(file->get_blocks()[0]->get_hash() == hash);
        REQUIRE(cluster->get_blocks().size() == 1);
        REQUIRE(cluster->get_blocks().by_hash(hash));
    }

    SECTION("identical blocks") {
        proto::set_type(pr_file, proto::FileInfoType::FILE);
        proto::set_size(pr_file, 5ul);
        proto::set_block_size(pr_file, 5ul);
        proto::set_sequence(pr_file, 1ul);

        auto hash = utils::sha256_digest(as_bytes("12345")).value();
        auto &pr_block = proto::add_blocks(pr_file);
        proto::set_size(pr_block, 5);
        proto::set_hash(pr_block, hash);
        REQUIRE(builder.local_update(folder->get_id(), pr_file).apply());

        auto folder_info = folder->get_folder_infos().by_device(*my_device);
        auto file = folder_info->get_file_infos().by_name(proto::get_name(pr_file));
        REQUIRE(file->get_sequence() == 1);
        CHECK(file->is_locally_available());

        proto::set_sequence(pr_file, 2ul);
        REQUIRE(builder.local_update(folder->get_id(), pr_file).apply());
        file = folder_info->get_file_infos().by_name(proto::get_name(pr_file));
        REQUIRE(file->get_sequence() == 2);
        CHECK(file->is_locally_available());
    }

    SECTION("delete file with blocks") {
        proto::set_type(pr_file, proto::FileInfoType::FILE);
        proto::set_size(pr_file, 5ul);
        proto::set_block_size(pr_file, 5ul);
        proto::set_sequence(pr_file, 1ul);

        auto hash = utils::sha256_digest(as_bytes("12345")).value();
        auto &pr_block = proto::add_blocks(pr_file);
        proto::set_size(pr_block, 5);
        proto::set_hash(pr_block, hash);

        auto folder_info = folder->get_folder_infos().by_device(*my_device);
        auto &files = folder_info->get_file_infos();
        auto &blocks = cluster->get_blocks();
        REQUIRE(builder.local_update(folder->get_id(), pr_file).apply());
        REQUIRE(files.size() == 1);
        REQUIRE(blocks.size() == 1);

        auto file = folder_info->get_file_infos().by_name(proto::get_name(pr_file));
        auto sequence = file->get_sequence();
        auto version = file->get_version();

        proto::FileInfo pr_updated;
        proto::set_name(pr_updated, "a.txt");
        proto::set_deleted(pr_updated, true);
        REQUIRE(builder.local_update(folder->get_id(), pr_updated).apply());
        REQUIRE(files.size() == 1);
        CHECK(blocks.size() == 0);

        file = file = folder_info->get_file_infos().by_name(proto::get_name(pr_file));
        CHECK(file->is_deleted());
        CHECK(file->get_blocks().size() == 0);
        CHECK(file->get_sequence() > sequence);
    }
}
