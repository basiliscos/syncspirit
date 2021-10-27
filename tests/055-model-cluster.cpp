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
