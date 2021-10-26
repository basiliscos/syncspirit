#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/misc/file_iterator.h"
#include "model/diff/load/blocks.h"
#include "model/diff/load/devices.h"
#include "model/diff/load/ignored_devices.h"
#include "model/diff/load/ignored_folders.h"
#include "model/diff/load/file_infos.h"
#include "model/diff/load/folder_infos.h"
#include "model/diff/load/folders.h"
#include "db/prefix.h"
#include "structs.pb.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

namespace bfs = boost::filesystem;

TEST_CASE("loading cluster (base)", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_ptr_t(new local_device_t(my_id, "my-device"));
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    CHECK(cluster);

    auto self_key = my_device->get_key();
    auto self_data = my_device->serialize();

    SECTION("local device") {
        diff::load::container_t devices;
        devices.emplace_back(diff::load::pair_t{self_key, self_data});
        auto diff = diff::cluster_diff_ptr_t(new diff::load::devices_t(devices));
        diff->apply(*cluster);
        auto& devices_map = cluster->get_devices();
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
        std::string data = db_device.SerializeAsString();

        device_ptr_t peer;

        SECTION("directly") {
            peer = device_ptr_t(new device_t(key, data));
        }

        SECTION("via diff (+ local device)") {
            diff::load::container_t devices;
            devices.emplace_back(diff::load::pair_t{key, data});
            devices.emplace_back(diff::load::pair_t{self_key, self_data});
            auto diff = diff::cluster_diff_ptr_t(new diff::load::devices_t(devices));
            diff->apply(*cluster);
            auto& devices_map = cluster->get_devices();
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

        auto block = block_info_ptr_t(new block_info_t(bi));
        auto key = block->get_key();
        auto data = block->serialize();

        auto target_block = block_info_ptr_t();

        SECTION("directly") {
            target_block = new block_info_t(key, data);
        }

        SECTION("via diff") {
            diff::load::container_t blocks;
            blocks.emplace_back(diff::load::pair_t{key, data});
            auto diff = diff::cluster_diff_ptr_t(new diff::load::blocks_t(blocks));
            diff->apply(*cluster);
            auto& blocks_map = cluster->get_blocks();
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
        auto device_id = device_id_t::from_string("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7").value();

        auto id = ignored_device_ptr_t(new ignored_device_t(device_id));
        auto key = id->get_key();
        auto data = id->serialize();

        auto target = ignored_device_ptr_t();

        SECTION("directly") {
            target = ignored_device_ptr_t(new ignored_device_t(key, data));
        }

        SECTION("via diff") {
            diff::load::container_t devices;
            devices.emplace_back(diff::load::pair_t{key, data});
            auto diff = diff::cluster_diff_ptr_t(new diff::load::ignored_devices_t(devices));
            diff->apply(*cluster);
            auto& map = cluster->get_ignored_devices();
            REQUIRE(map.size() == 1);
            target = map.get(device_id.get_sha256());
        }

        REQUIRE(target);
        CHECK(target->get_sha256() == id->get_sha256());
        CHECK(target->get_key() == id->get_key());
    }

    SECTION("folders") {
        db::Folder db_folder;
        db_folder.set_id("1234-5678");
        db_folder.set_label("my-label");
        db_folder.set_path("/my/path");

        auto uuid = cluster->next_uuid();
        auto prefix = (char)db::prefix::folder;
        auto id = std::string("1234-5678");
        auto key = std::string(&prefix, 1) + std::string(uuid.begin(), uuid.end());
        auto data = db_folder.SerializeAsString();

        auto folder = folder_ptr_t{};

        SECTION("directly") {
            folder = folder_ptr_t(new folder_t(key, data));
        }

        SECTION("via diff") {
            diff::load::container_t folders;
            folders.emplace_back(diff::load::pair_t{key, data});
            auto diff = diff::cluster_diff_ptr_t(new diff::load::folders_t(folders));
            diff->apply(*cluster);
            auto& map = cluster->get_folders();
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

        auto folder = ignored_folder_ptr_t(new ignored_folder_t(std::string("folder-id"), "my-label"));
        auto key = folder ->get_key();
        auto data = folder ->serialize();

        auto target = ignored_folder_ptr_t();

        SECTION("directly") {
            target = ignored_folder_ptr_t(new ignored_folder_t(key, data));
        }

        SECTION("via diff") {
            diff::load::container_t folders;
            folders.emplace_back(diff::load::pair_t{key, data});
            auto diff = diff::cluster_diff_ptr_t(new diff::load::ignored_folders_t(folders));
            diff->apply(*cluster);
            auto& map = cluster->get_ignored_folders();
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
    auto my_device = device_ptr_t(new local_device_t(my_id, "my-device"));
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    CHECK(cluster);
    cluster->get_devices().put(my_device);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");

    auto uuid = cluster->next_uuid();
    auto prefix = (char)db::prefix::folder;
    auto folder_key = std::string(&prefix, 1) + std::string(uuid.begin(), uuid.end());
    auto folder_data = db_folder.SerializeAsString();

    auto folder = folder_ptr_t(new folder_t(folder_key, folder_data));
    cluster->get_folders().put(folder);

    db::FolderInfo db_fi;
    db_fi.set_index_id(2);
    db_fi.set_max_sequence(3);
    auto data = db_fi.SerializeAsString();
    auto fi = folder_info_ptr_t(new folder_info_t(cluster->next_uuid(), data, my_device, folder));
    CHECK(fi);
    CHECK(fi->get_index() == 2ul);
    CHECK(fi->get_max_sequence() == 3ul);

    auto target = folder_info_ptr_t();

    SECTION("directly") {
        target = new folder_info_t(fi->get_key(), data, my_device, folder);
    }

    SECTION("via diff") {
        diff::load::container_t folders;
        auto data = fi->serialize();
        folders.emplace_back(diff::load::pair_t{fi->get_key(), data});
        auto diff = diff::cluster_diff_ptr_t(new diff::load::folder_infos_t(folders));
        diff->apply(*cluster);
        auto& map = folder->get_folder_infos();
        REQUIRE(map.size() == 1);
        target = map.get(fi->get_uuid());
        REQUIRE(map.by_device(my_device));
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
    auto my_device = device_ptr_t(new local_device_t(my_id, "my-device"));
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    CHECK(cluster);
    cluster->get_devices().put(my_device);

    auto bi = proto::BlockInfo();
    bi.set_size(5);
    bi.set_hash(utils::sha256_digest("12345").value());
    auto block = block_info_ptr_t(new block_info_t(bi));
    auto& blocks_map = cluster->get_blocks();
    blocks_map.put(block);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");

    auto uuid = cluster->next_uuid();
    auto folder_prefix = (char)db::prefix::folder;
    auto folder_key = std::string(&folder_prefix, 1) + std::string(uuid.begin(), uuid.end());
    auto folder_data = db_folder.SerializeAsString();

    auto folder = folder_ptr_t(new folder_t(folder_key, folder_data));
    cluster->get_folders().put(folder);

    db::FolderInfo db_folder_info;
    db_folder_info.set_index_id(2);
    db_folder_info.set_max_sequence(3);
    auto folder_info_data = db_folder_info.SerializeAsString();
    auto folder_info = folder_info_ptr_t(new folder_info_t(cluster->next_uuid(), folder_info_data, my_device, folder));
    auto folder_info_db = db::FolderInfo();
    folder_info_db.ParseFromArray(folder_info_data.c_str(), folder_info_data.size());
    CHECK(folder_info);
    CHECK(folder_info->get_index() == 2ul);
    CHECK(folder_info->get_max_sequence() == 3ul);
    folder->get_folder_infos().put(folder_info);

    proto::FileInfo pr_fi;
    pr_fi.set_name("a/b.txt");
    pr_fi.set_size(55ul);
    auto fi = file_info_ptr_t(new file_info_t(cluster->next_uuid(),  pr_fi, folder_info));
    CHECK(fi);
    fi->add_block(block);

    auto target = file_info_ptr_t();

    SECTION("directly") {
        auto data = fi->serialize(true);
        db::FileInfo file_info_db;
        file_info_db.ParseFromArray(data.data(), data.size());
        target = new file_info_t(fi->get_key(), &file_info_db, folder_info);
        REQUIRE(target);
        CHECK(target->get_blocks().size() == 0ul);
    }

    SECTION("via diff") {
        diff::load::container_t container;
        auto data = fi->serialize(true);
        container.emplace_back(diff::load::pair_t{fi->get_key(), data});
        auto diff = diff::cluster_diff_ptr_t(new diff::load::file_infos_t(container));
        diff->apply(*cluster);
        auto& map = folder_info->get_file_infos();
        REQUIRE(map.size() == 1);
        target = map.get(fi->get_uuid());
        REQUIRE(target);
        REQUIRE(map.by_name(fi->get_name()));
        REQUIRE(target->get_blocks().size() == 1);
        REQUIRE(target->get_blocks().begin()->get()->get_hash() == block->get_hash());
    }

    CHECK(target->get_key() == fi->get_key());
    CHECK(target->get_name() == fi->get_name());
    CHECK(target->get_full_name() == fi->get_full_name());
    CHECK(target->get_full_name() == "my-label/a/b.txt");
}


#if 0
TEST_CASE("iterate_files", "[model]") {
    std::uint64_t key = 0;
    db::Device db_d1;
    db_d1.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
    db_d1.set_name("d1");
    auto device_my = model::device_ptr_t(new model::device_t(db_d1, ++key));

    db::Device db_d2;
    db_d2.set_id(test::device_id2sha256("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7"));
    db_d2.set_name("d2");
    auto device_peer = model::device_ptr_t(new model::device_t(db_d2, ++key));

    db::Folder db_f1;
    db_f1.set_id("f1");
    db_f1.set_label("f1-l");
    db_f1.set_path("/some/path/d1");
    auto f1 = model::folder_ptr_t(new model::folder_t(db_f1, ++key));

    db::Folder db_f2;
    db_f2.set_id("f2");
    db_f2.set_label("f2-l");
    db_f2.set_path("/some/path/d2");
    auto f2 = model::folder_ptr_t(new model::folder_t(db_f2, ++key));

    auto folders = model::folders_map_t();
    folders.put(f1);
    folders.put(f2);

    cluster_ptr_t cluster = new cluster_t(device_my);
    cluster->assign_folders(std::move(folders));

    SECTION("cluster config serialization") {
        std::int64_t seq = 1;
        db::FolderInfo db_folder_info;
        db_folder_info.set_max_sequence(++seq);

        auto fi1 = model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, device_my.get(), f1.get(), ++seq));
        f1->add(fi1);

        auto fi2 = model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, device_peer.get(), f2.get(), ++seq));
        f1->add(fi2);

        auto config = cluster->get(device_peer);
        REQUIRE(config.folders_size() == 1);
        auto &f = config.folders(0);
        CHECK(f.label() == f1->label());
        CHECK(f.id() == f1->id());

        REQUIRE(f.devices_size() == 2);
        auto &sd1 = f.devices(0);
        auto &sd2 = f.devices(1);
        CHECK(((sd1.name() == device_my->name) || (sd1.name() == device_peer->name)));
        CHECK(((sd2.name() == device_my->name) || (sd2.name() == device_peer->name)));
    }

    SECTION("apply peer's cluster config with new index") {
        db::FolderInfo db_fi;
        db_fi.set_max_sequence(++key);
        db_fi.set_index_id(++key);
        auto fi_peer = folder_info_ptr_t(new folder_info_t(db_fi, device_peer.get(), f1.get(), ++key));
        f1->add(fi_peer);

        db::BlockInfo db_b1;
        db_b1.set_hash("hash-1");
        db_b1.set_size(5);
        auto b1 = model::block_info_ptr_t(new model::block_info_t(db_b1, ++key));

        cluster->get_blocks().put(b1);

        auto db_file = db::FileInfo();
        db_file.set_name("my-file.txt");
        db_file.set_sequence(++key);
        db_file.set_type(proto::FileInfoType::FILE);
        db_file.add_blocks_keys(b1->get_db_key());
        auto file = model::file_info_ptr_t(new file_info_t(db_file, fi_peer.get()));
        fi_peer->add(file);

        REQUIRE(file->get_blocks().size() == 1);
        REQUIRE(file->get_blocks()[0] == b1);

        proto::ClusterConfig p_config;
        auto p_folder = p_config.add_folders();
        p_folder->set_id(f1->id());
        p_folder->set_label(f1->label());

        auto new_index_id = ++key;
        auto p_device = p_folder->add_devices();
        p_device->set_id(device_peer->device_id.get_sha256());
        p_device->set_max_sequence(++key);
        p_device->set_index_id(new_index_id);

        auto r = cluster->update(p_config);
        CHECK(r.unknown_folders.size() == 0);
        CHECK(r.outdated_folders.size() == 1);
    }
}
#endif
