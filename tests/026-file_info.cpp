// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include "test-utils.h"
#include "model/cluster.h"
#include "model/file_info.h"
#include "model/misc/sequencer.h"
#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::test;

using Catch::Matchers::Matches;

TEST_CASE("file-info", "[model]") {
    auto sequencer = make_sequencer(4);
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));

    db::Folder db_folder;
    db::set_id(db_folder, "1234-5678");
    db::set_label(db_folder, "my-label");
    db::set_path(db_folder, "/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();
    folder->assign_cluster(cluster);

    db::FolderInfo db_folder_info;
    db::set_index_id(db_folder_info, 2);
    db::set_max_sequence(db_folder_info, 3);
    auto folder_info = folder_info_t::create(sequencer->next_uuid(), db_folder_info, my_device, folder).value();

    proto::FileInfo pr_fi;
    auto name = "a/b.txt";
    proto::set_name(pr_fi, name);
    proto::set_size(pr_fi, 55ul);
    proto::set_block_size(pr_fi, 5ul);
    proto::set_sequence(pr_fi, 6);
    proto::set_modified_s(pr_fi, 1734680712);
    proto::add_counters(proto::get_version(pr_fi), proto::Counter(my_device->device_id().get_uint(), 0));

    auto fi = file_info_t::create(sequencer->next_uuid(), pr_fi, folder_info).value();
    auto map = file_infos_map_t{};

    map.put(fi);
    CHECK(map.by_name(name) == fi);
    CHECK(map.by_sequence(proto::get_sequence(pr_fi)) == fi);

    auto [begin, end] = map.range(0, 10);
    CHECK(std::distance(begin, end) == 1);
    CHECK(*begin == fi);

    map.remove(fi);
    fi->set_sequence(10);

    map.put(fi);
    CHECK(map.by_name(proto::get_name(pr_fi)) == fi);
    CHECK(map.by_sequence(10) == fi);
    CHECK(!map.by_sequence(proto::get_sequence(pr_fi)));

    auto conflict_name = fi->make_conflicting_name();
    REQUIRE_THAT(conflict_name, Matches("a.b.sync-conflict-202412(\\d){2}-(\\d){6}-KHQNO2S.txt"));
}

TEST_CASE("file_info_t::local_file", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto folder_my = folder_infos.by_device(*my_device);
    auto folder_peer = folder_infos.by_device(*peer_device);

    auto pr_file = proto::FileInfo();
    proto::set_name(pr_file, "a.txt");

    auto &v = proto::get_version(pr_file);
    proto::add_counters(v, proto::Counter(1, 1));

    SECTION("no local file") {
        auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
        CHECK(!folder_my->get_file_infos().by_name("a.txt"));
        CHECK(file_peer->is_locally_available());
    }

    SECTION("there is identical local file") {
        proto::set_sequence(pr_file, folder_my->get_max_sequence() + 1);
        auto file_my = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file_my));

        proto::set_sequence(pr_file, folder_peer->get_max_sequence() + 1);
        auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
        REQUIRE(folder_peer->add_strict(file_peer));

        auto lf = folder_my->get_file_infos().by_name(file_peer->get_name()->get_full_name());
        REQUIRE(lf);
        REQUIRE(lf == file_my);
    }
}

TEST_CASE("file_info_t::check_consistency", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto folder_my = folder_infos.by_device(*my_device);

    auto pr_file = proto::FileInfo();
    proto::set_name(pr_file, "a.txt");
    proto::set_block_size(pr_file, 5);
    proto::set_size(pr_file, 5);

    auto b_hash = utils::sha256_digest(as_bytes("12345")).value();
    auto &b = proto::add_blocks(pr_file);
    proto::set_hash(b, b_hash);
    proto::set_size(b, 5);

    auto bi = block_info_t::create(b).value();
    auto &blocks_map = cluster->get_blocks();
    blocks_map.put(bi);
    auto bbb = blocks_map.by_hash(b_hash);
    REQUIRE(bbb);

    REQUIRE(builder.local_update(folder->get_id(), pr_file).apply());
    auto file_my = folder_my->get_file_infos().by_name(proto::get_name(pr_file));
    CHECK(file_my->get_size() == 5);
}

TEST_CASE("file_info_t::create, inconsistent source", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto folder_my = folder_infos.by_device(*my_device);

    auto pr_block = proto::BlockInfo();
    proto::set_size(pr_block, 131072);

    auto pr_file = proto::FileInfo();
    proto::set_name(pr_file, "a.txt");
    proto::set_block_size(pr_file, 131072);
    proto::set_size(pr_file, 0);

    auto &v = proto::get_version(pr_file);
    proto::add_counters(v, proto::Counter(my_device->device_id().get_uint(), 1));
    proto::add_blocks(pr_file, pr_block);

    auto my_file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
    CHECK(my_file->get_block_size() == 0);
    CHECK(my_file->iterate_blocks().get_total() == 0);
}

TEST_CASE("file_info_t::get_size", "[model]") {
    auto local_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto local_device = device_t::create(local_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(local_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(local_device);

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto folder_local = folder_infos.by_device(*local_device);

    auto b_hash = utils::sha256_digest(as_bytes("12345")).value();
    auto pr_block = proto::BlockInfo();
    proto::set_size(pr_block, 5);
    proto::set_hash(pr_block, b_hash);

    auto b_2_hash = utils::sha256_digest(as_bytes("11111")).value();
    auto pr_block_2 = proto::BlockInfo();
    proto::set_size(pr_block_2, 5);
    proto::set_hash(pr_block_2, b_2_hash);
    proto::set_offset(pr_block_2, 5);

    auto bi1 = block_info_t::create(pr_block).value();
    auto bi2 = block_info_t::create(pr_block_2).value();

    auto pr_file = proto::FileInfo();
    proto::set_name(pr_file, "a.txt");

    auto v = proto::get_version(pr_file);
    proto::add_counters(v, proto::Counter(local_device->device_id().get_uint(), 1));
    proto::set_version(pr_file, v);

    SECTION("1 block file") {
        proto::set_block_size(pr_file, 5);
        proto::set_size(pr_file, 5);
        proto::add_blocks(pr_file, pr_block);

        auto local_file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_local).value();
        local_file->assign_block(bi1.get(), 0);
        CHECK(local_file->get_size() == 5);
    }
    SECTION("0 block file") {
        auto local_file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_local).value();
        CHECK(local_file->get_size() == 0);
    }
    SECTION("2 equi-sized block file") {
        proto::set_block_size(pr_file, 5);
        proto::set_size(pr_file, 10);

        proto::add_blocks(pr_file, pr_block);
        proto::add_blocks(pr_file, pr_block_2);

        auto local_file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_local).value();
        local_file->assign_block(bi1.get(), 0);
        local_file->assign_block(bi2.get(), 1);
        CHECK(local_file->get_size() == 10);
    }
}

TEST_CASE("file_info_t::update", "[model]") {
    auto local_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto local_device = device_t::create(local_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(local_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(local_device);

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto folder_local = folder_infos.by_device(*local_device);

    auto pr_block = proto::BlockInfo();
    proto::set_size(pr_block, 5);
    auto bi1 = block_info_t::create(pr_block).value();

    SECTION("update to emtpy") {
        auto pr_file_1 = proto::FileInfo();
        proto::set_name(pr_file_1, "a.txt");
        proto::set_block_size(pr_file_1, 5);
        proto::set_size(pr_file_1, 5);

        auto v = proto::get_version(pr_file_1);
        proto::add_counters(v, proto::Counter(local_device->device_id().get_uint(), 1));
        proto::add_blocks(pr_file_1, pr_block);
        proto::set_version(pr_file_1, v);

        auto local_file_1 = file_info_t::create(sequencer->next_uuid(), pr_file_1, folder_local).value();
        local_file_1->assign_block(bi1.get(), 0);
        CHECK(local_file_1->get_size() == 5);

        auto pr_file_2 = proto::FileInfo();
        proto::set_name(pr_file_2, "a.txt");
        proto::set_version(pr_file_2, v);

        auto uuid = bu::uuid{};
        assign(uuid, local_file_1->get_uuid());
        auto local_file_2 = file_info_t::create(uuid, pr_file_2, folder_local).value();
        CHECK(local_file_2->get_size() == 0);

        local_file_1->update(*local_file_2);
        CHECK(local_file_1->get_size() == 0);
        CHECK(local_file_1->iterate_blocks().get_total() == 0);
    }

    SECTION("update to symlink") {
        auto pr_file_1 = proto::FileInfo();
        proto::set_name(pr_file_1, "a.txt");
        proto::set_block_size(pr_file_1, 5);
        proto::set_size(pr_file_1, 5);

        auto v = proto::get_version(pr_file_1);
        proto::add_counters(v, proto::Counter(local_device->device_id().get_uint(), 1));
        proto::add_blocks(pr_file_1, pr_block);
        proto::set_version(pr_file_1, v);

        auto local_file_1 = file_info_t::create(sequencer->next_uuid(), pr_file_1, folder_local).value();
        local_file_1->assign_block(bi1.get(), 0);
        CHECK(local_file_1->get_size() == 5);
        CHECK(local_file_1->is_file());

        auto pr_file_2 = proto::FileInfo();
        proto::set_name(pr_file_2, "a.txt");
        proto::set_type(pr_file_2, proto::FileInfoType::SYMLINK);
        proto::set_version(pr_file_2, v);
        proto::set_symlink_target(pr_file_2, "b.txt");

        auto uuid = bu::uuid{};
        assign(uuid, local_file_1->get_uuid());
        auto local_file_2 = file_info_t::create(uuid, pr_file_2, folder_local).value();
        CHECK(local_file_2->get_size() == 0);
        CHECK(local_file_2->is_link());

        local_file_1->update(*local_file_2);
        CHECK(local_file_1->get_size() == 0);
        CHECK(local_file_1->iterate_blocks().get_total() == 0);
        CHECK(local_file_1->is_link());
    }
}
