// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/load/blocks.h"
#include "model/diff/load/devices.h"
#include "model/diff/load/ignored_devices.h"
#include "model/diff/load/ignored_folders.h"
#include "model/diff/load/file_infos.h"
#include "model/diff/load/folder_infos.h"
#include "model/diff/load/folders.h"
#include "db/prefix.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("loading cluster (base)", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1, 1));
    CHECK(cluster);

    auto self_key = my_device->get_key();
    auto self_data = my_device->serialize();

    SECTION("local device") {
        diff::load::container_t devices;
        devices.emplace_back(diff::load::pair_t{self_key, self_data});
        auto diff = diff::cluster_diff_ptr_t(new diff::load::devices_t(devices));
        REQUIRE(diff->apply(*cluster));
        auto &devices_map = cluster->get_devices();
        REQUIRE(devices_map.size() == 1);
        auto self = devices_map.get(self_key);
        auto self_2 = devices_map.by_sha256(my_id.get_sha256());
        REQUIRE(self);
        REQUIRE(self_2);
        CHECK(self == my_device);
        CHECK(self_2 == self);
    }

    SECTION("devices") {
        auto prefix = (char)db::prefix::device;
        auto device_id = test::device_id2sha256("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7");
        std::string key = std::string(&prefix, 1) + device_id;
        db::Device db_device;
        db_device.set_cert_name("cn");
        db_device.set_name("peer-name");

        device_ptr_t peer;

        SECTION("directly") { peer = device_t::create(key, db_device).value(); }

        SECTION("via diff (+ local device)") {
            std::string data = db_device.SerializeAsString();
            diff::load::container_t devices;
            devices.emplace_back(diff::load::pair_t{key, data});
            devices.emplace_back(diff::load::pair_t{self_key, self_data});
            auto diff = diff::cluster_diff_ptr_t(new diff::load::devices_t(devices));
            REQUIRE(diff->apply(*cluster));
            auto &devices_map = cluster->get_devices();
            REQUIRE(devices_map.size() == 2);
            peer = devices_map.get(key);
            auto peer_2 = devices_map.by_sha256(device_id);
            CHECK(peer_2 == peer);

            auto self = devices_map.get(my_device->get_key());
            auto self_2 = devices_map.by_sha256(my_id.get_sha256());
            CHECK(self == my_device);
            CHECK(self_2 == self);
        }

        REQUIRE(peer);
        CHECK(peer->get_name() == "peer-name");
        CHECK(peer->get_cert_name());
        CHECK(peer->get_cert_name().value() == "cn");
    }

    SECTION("blocks") {
        auto bi = proto::BlockInfo();
        bi.set_size(5);
        bi.set_hash(utils::sha256_digest("12345").value());

        auto block = block_info_t::create(bi).assume_value();
        auto key = block->get_key();

        auto target_block = block_info_ptr_t();

        SECTION("via diff") {
            auto data = block->serialize();
            diff::load::container_t blocks;
            blocks.emplace_back(diff::load::pair_t{key, data});
            auto diff = diff::cluster_diff_ptr_t(new diff::load::blocks_t(blocks));
            REQUIRE(diff->apply(*cluster));
            auto &blocks_map = cluster->get_blocks();
            REQUIRE(blocks_map.size() == 1);
            target_block = blocks_map.get(bi.hash());
        }

        REQUIRE(target_block);
        CHECK(target_block->get_hash() == block->get_hash());
        CHECK(target_block->get_key() == block->get_key());
        CHECK(target_block->get_weak_hash() == block->get_weak_hash());
        CHECK(target_block->get_size() == block->get_size());
    }

    SECTION("ignored_devices") {
        db::SomeDevice db_device;
        db_device.set_label("my-label");
        db_device.set_address("tcp://127.0.0.1");
        db_device.set_last_seen(0);

        auto device_id =
            device_id_t::from_string("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7").value();

        auto id = ignored_device_t::create(device_id, db_device).value();
        auto key = id->get_key();
        auto data = id->serialize();

        SECTION("via diff") {
            diff::load::container_t devices;
            devices.emplace_back(diff::load::pair_t{key, data});
            auto diff = diff::cluster_diff_ptr_t(new diff::load::ignored_devices_t(devices));
            REQUIRE(diff->apply(*cluster));
            auto &map = cluster->get_ignored_devices();
            REQUIRE(map.size() == 1);

            auto target = map.get(device_id.get_sha256());
            REQUIRE(target);
            CHECK(target->get_sha256() == id->get_sha256());
            CHECK(target->get_key() == id->get_key());
        }
    }

    SECTION("folders") {
        db::Folder db_folder;
        db_folder.set_id("1234-5678");
        db_folder.set_label("my-label");
        db_folder.set_path("/my/path");

        auto uuid = cluster->next_uuid();
        auto id = std::string("1234-5678");

        auto folder = folder_t::create(uuid, db_folder).value();

        SECTION("via diff") {
            auto data = db_folder.SerializeAsString();
            diff::load::container_t folders;
            auto key = folder->get_key();
            folders.emplace_back(diff::load::pair_t{key, data});
            auto diff = diff::cluster_diff_ptr_t(new diff::load::folders_t(folders));
            REQUIRE(diff->apply(*cluster));
            auto &map = cluster->get_folders();
            REQUIRE(map.size() == 1);
            auto f1 = map.begin()->item;
            folder = map.get(key);
            REQUIRE(folder);
            REQUIRE(folder == map.by_id(id));
            CHECK(folder->get_cluster() == cluster.get());
        }

        REQUIRE(folder);
        CHECK(folder->get_id() == id);
        CHECK(folder->get_label() == "my-label");
        CHECK(folder->get_path() == "/my/path");
    }

    SECTION("ignored folders") {
        db::IgnoredFolder db_folder;
        db_folder.set_label("my-label");

        auto folder = ignored_folder_t::create(std::string("folder-id"), "my-label").value();
        auto key = folder->get_key();
        auto data = folder->serialize();

        auto target = ignored_folder_ptr_t();

        SECTION("directly") { target = ignored_folder_t::create(key, data).value(); }

        SECTION("via diff") {
            diff::load::container_t folders;
            folders.emplace_back(diff::load::pair_t{key, data});
            auto diff = diff::cluster_diff_ptr_t(new diff::load::ignored_folders_t(folders));
            REQUIRE(diff->apply(*cluster));
            auto &map = cluster->get_ignored_folders();
            REQUIRE(map.size() == 1);
            target = map.get(folder->get_id());
        }

        REQUIRE(target);

        CHECK(target->get_id() == folder->get_id());
        CHECK(folder->get_key() == folder->get_key());
        CHECK(folder->get_label() == folder->get_label());
    }
}

TEST_CASE("loading cluster (folder info)", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1, 1));
    CHECK(cluster);
    cluster->get_devices().put(my_device);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");

    auto uuid = cluster->next_uuid();
    auto folder = folder_t::create(uuid, db_folder).value();
    cluster->get_folders().put(folder);

    db::FolderInfo db_fi;
    db_fi.set_index_id(2);
    db_fi.set_max_sequence(3);
    auto fi = folder_info_t::create(cluster->next_uuid(), db_fi, my_device, folder).value();
    CHECK(fi);
    CHECK(fi->get_index() == 2ul);
    CHECK(fi->get_max_sequence() == 3ul);

    auto target = folder_info_ptr_t();

    SECTION("via diff") {
        diff::load::container_t folders;
        auto data = fi->serialize();
        folders.emplace_back(diff::load::pair_t{fi->get_key(), data});
        auto diff = diff::cluster_diff_ptr_t(new diff::load::folder_infos_t(folders));
        REQUIRE(diff->apply(*cluster));
        auto &map = folder->get_folder_infos();
        REQUIRE(map.size() == 1);
        target = map.get(fi->get_uuid());
        REQUIRE(map.by_device(*my_device));
    }

    REQUIRE(target);
    CHECK(target->get_key() == fi->get_key());
    CHECK(target->get_uuid() == fi->get_uuid());
    CHECK(target->get_device() == fi->get_device());
    CHECK(target->get_folder() == fi->get_folder());
    CHECK(target->get_index() == fi->get_index());
    CHECK(target->get_max_sequence() == fi->get_max_sequence());
}

TEST_CASE("loading cluster (file info + block)", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1, 1));
    CHECK(cluster);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto bi = proto::BlockInfo();
    bi.set_size(5);
    bi.set_hash(utils::sha256_digest("12345").value());
    auto block = block_info_t::create(bi).assume_value();
    auto &blocks_map = cluster->get_blocks();
    blocks_map.put(block);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");

    auto uuid = cluster->next_uuid();
    auto folder = folder_t::create(uuid, db_folder).value();
    cluster->get_folders().put(folder);

    db::FolderInfo db_folder_info;
    db_folder_info.set_index_id(2);
    db_folder_info.set_max_sequence(3);
    auto folder_info = folder_info_t::create(cluster->next_uuid(), db_folder_info, my_device, folder).value();
    CHECK(folder_info);
    CHECK(folder_info->get_index() == 2ul);
    CHECK(folder_info->get_max_sequence() == 3ul);
    folder->get_folder_infos().put(folder_info);

    proto::FileInfo pr_fi;
    pr_fi.set_name("a/b.txt");
    pr_fi.set_size(55ul);
    pr_fi.set_block_size(5ul);
    auto fi = file_info_t::create(cluster->next_uuid(), pr_fi, folder_info).value();
    CHECK(fi);
    for (size_t i = 0; i < 11; ++i) {
        fi->assign_block(block, i);
    }

    auto target = file_info_ptr_t();

    SECTION("directly") {
        auto data = fi->serialize(true);
        db::FileInfo file_info_db;
        file_info_db.ParseFromArray(data.data(), data.size());
        target = file_info_t::create(fi->get_key(), file_info_db, folder_info).value();
        REQUIRE(target);
        CHECK(target->get_size() == 55ul);
        CHECK(target->get_block_size() == 5ul);
        CHECK(target->get_blocks().size() == 11ul);
        CHECK(!target->is_locked());
    }

    SECTION("via diff") {
        diff::load::container_t container;
        auto data = fi->serialize(true);
        container.emplace_back(diff::load::pair_t{fi->get_key(), data});
        auto diff = diff::cluster_diff_ptr_t(new diff::load::file_infos_t(container));
        REQUIRE(diff->apply(*cluster));
        auto &map = folder_info->get_file_infos();
        REQUIRE(map.size() == 1);
        target = map.get(fi->get_uuid());
        REQUIRE(target);
        REQUIRE(map.by_name(fi->get_name()));
        REQUIRE(target->get_blocks().size() == 11);
        REQUIRE(target->get_blocks().begin()->get()->get_hash() == block->get_hash());
        CHECK(!target->is_locked());
    }

    SECTION("via diff + lock when have a source") {
        db_folder_info.set_index_id(3);
        auto folder_peer = folder_info_t::create(cluster->next_uuid(), db_folder_info, peer_device, folder).value();
        folder->get_folder_infos().put(folder_peer);
        auto version = pr_fi.mutable_version();
        auto counter = version->add_counters();
        counter->set_id(5);
        counter->set_value(peer_device->as_uint());
        auto file_peer = file_info_t::create(cluster->next_uuid(), pr_fi, folder_peer).value();
        folder_peer->add(file_peer, false);

        diff::load::container_t container;
        fi->set_source(file_peer);
        auto data = fi->serialize(true);
        container.emplace_back(diff::load::pair_t{fi->get_key(), data});
        auto diff = diff::cluster_diff_ptr_t(new diff::load::file_infos_t(container));
        REQUIRE(diff->apply(*cluster));
        auto &map = folder_info->get_file_infos();
        REQUIRE(map.size() == 1);
        target = map.get(fi->get_uuid());
        REQUIRE(target);
        REQUIRE(map.by_name(fi->get_name()));
        REQUIRE(target->get_blocks().size() == 11);
        REQUIRE(target->get_blocks().begin()->get()->get_hash() == block->get_hash());
        CHECK(target->is_locked());
        CHECK(target->get_source() == file_peer);
    }

    CHECK(target->get_key() == fi->get_key());
    CHECK(target->get_name() == fi->get_name());
    CHECK(target->get_full_name() == fi->get_full_name());
    CHECK(target->get_full_name() == "my-label/a/b.txt");
}
