#include "catch.hpp"
#include "test-utils.h"
#include "model/file_info.h"
#include "model/block_iterator.h"
#include "model/cluster.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

namespace bfs = boost::filesystem;

TEST_CASE("linked file", "[model]") {
    std::uint64_t key = 0;
    db::Device db_d1;
    db_d1.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
    db_d1.set_name("d1");
    auto d1 = model::device_ptr_t(new model::device_t(db_d1, ++key));

    db::Device db_d2;
    db_d2.set_id(test::device_id2sha256("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7"));
    db_d2.set_name("d2");
    auto d2 = model::device_ptr_t(new model::device_t(db_d2, ++key));

    db::Folder db_f1;
    db_f1.set_id("f1");
    db_f1.set_label("f1-l");
    db_f1.set_path("/some/path/d1");
    auto f1 = model::folder_ptr_t(new model::folder_t(db_f1, ++key));

    auto folders = model::folders_map_t();
    folders.put(f1);

    db::FolderInfo db_folder_info;
    db_folder_info.set_max_sequence(++key);
    auto fi1_my = model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, d1.get(), f1.get(), ++key));
    auto fi1_peer = model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, d2.get(), f1.get(), ++key));
    f1->add(fi1_my);
    f1->add(fi1_peer);

    cluster_ptr_t cluster = new cluster_t(d1);
    cluster->assign_folders(std::move(folders));

    db::BlockInfo db_b1;
    db_b1.set_hash("h1");
    db_b1.set_size(5);
    db_b1.set_weak_hash(23u);
    auto b1 = model::block_info_ptr_t(new model::block_info_t(db_b1, ++key));

    db::BlockInfo db_b2;
    db_b2.set_hash("h2");
    db_b2.set_size(5);
    db_b2.set_weak_hash(23u);
    auto b2 = model::block_info_ptr_t(new model::block_info_t(db_b2, ++key));

    db::BlockInfo db_b3;
    db_b3.set_hash("h3");
    db_b3.set_size(5);
    db_b3.set_weak_hash(23u);
    auto b3 = model::block_info_ptr_t(new model::block_info_t(db_b3, ++key));

    cluster->get_blocks().put(b1);
    cluster->get_blocks().put(b2);
    cluster->get_blocks().put(b3);

    db::FileInfo db_file_11;
    db_file_11.set_sequence(++key);
    db_file_11.set_name("zzz");
    db_file_11.set_size(1024);
    db_file_11.add_blocks_keys(b1->get_db_key());
    db_file_11.add_blocks_keys(b2->get_db_key());
    auto source = model::file_info_ptr_t(new model::file_info_t(db_file_11, fi1_peer.get()));
    fi1_peer->add(source);
    SECTION("target file is missing") {
        auto target = source->link(d1);
        REQUIRE(target);
        CHECK(target->is_locked());
        CHECK(target->is_dirty());
        CHECK(target->is_incomplete());
        CHECK(target->get_name() == "zzz");
        CHECK(target->get_sequence() == source->get_sequence());
        CHECK(target->get_size() == source->get_size());
        CHECK(target->get_blocks().size() == source->get_blocks().size());
        CHECK(!target->get_blocks()[0]);
        CHECK(!target->get_blocks()[1]);
    }

    SECTION("target file is outdated") {
        db::FileInfo db_file_12;
        db_file_12.set_sequence(db_file_11.sequence() - 1);
        db_file_12.set_name("zzz");
        db_file_12.set_size(1024);
        db_file_12.add_blocks_keys(b1->get_db_key());
        auto target = model::file_info_ptr_t(new model::file_info_t(db_file_12, fi1_peer.get()));
        fi1_my->add(target);

        auto t2 = source->link(d1);
        REQUIRE(t2);
        CHECK(t2 != target);
        CHECK(t2->is_locked());
        CHECK(t2->is_dirty());
        CHECK(t2->is_incomplete());
        CHECK(t2->get_db_key() != target->get_db_key());
        CHECK(t2->get_name() == "zzz");
        CHECK(t2->get_sequence() == source->get_sequence());
        CHECK(t2->get_size() == source->get_size());
        CHECK(t2->get_blocks().size() == source->get_blocks().size());
        CHECK(!t2->get_blocks()[0]);
        CHECK(!t2->get_blocks()[1]);
    }

    SECTION("target file is incomplete") {
        db::FileInfo db_file_12;
        db_file_12.set_sequence(db_file_11.sequence());
        db_file_12.set_name("zzz");
        db_file_12.set_size(1024);
        db_file_12.add_blocks_keys(b1->get_db_key());
        auto target = model::file_info_ptr_t(new model::file_info_t(db_file_12, fi1_my.get()));
        target->mark_incomplete();
        fi1_my->add(target);
        REQUIRE(target->get_blocks().size() == 1);

        auto t2 = source->link(d1);
        REQUIRE(t2);
        CHECK(t2 == target);
        CHECK(t2->is_locked());
        CHECK(!t2->is_dirty());
        REQUIRE(t2->get_blocks().size() == 2);
        CHECK(t2->get_blocks()[0] == b1);
        CHECK(!t2->get_blocks()[1]);
    }

    SECTION("target file is incomplete, has a garbage blocks") {
        db::FileInfo db_file_12;
        db_file_12.set_sequence(db_file_11.sequence());
        db_file_12.set_name("zzz");
        db_file_12.set_size(1024);
        db_file_12.add_blocks_keys(b3->get_db_key());
        auto target = model::file_info_ptr_t(new model::file_info_t(db_file_12, fi1_my.get()));
        target->mark_incomplete();
        fi1_my->add(target);
        REQUIRE(target->get_blocks().size() == 1);

        auto t2 = source->link(d1);
        REQUIRE(t2);
        CHECK(t2 == target);
        CHECK(t2->is_locked());
        CHECK(!t2->is_dirty());
        REQUIRE(t2->get_blocks().size() == 2);
        CHECK(!t2->get_blocks()[0]);
        CHECK(!t2->get_blocks()[1]);
    }

    SECTION("target file has different size") {
        db::FileInfo db_file_12;
        db_file_12.set_sequence(db_file_11.sequence());
        db_file_12.set_name("zzz");
        db_file_12.set_size(1024 * 2);
        db_file_12.add_blocks_keys(b2->get_db_key());
        auto target = model::file_info_ptr_t(new model::file_info_t(db_file_12, fi1_my.get()));
        target->mark_incomplete();
        fi1_my->add(target);
        REQUIRE(target->get_blocks().size() == 1);

        auto t2 = source->link(d1);
        REQUIRE(t2);
        CHECK(t2 != target);
        CHECK(t2->is_locked());
        CHECK(t2->is_dirty());
        REQUIRE(t2->get_blocks().size() == 2);
        CHECK(!t2->get_blocks()[0]);
        CHECK(!t2->get_blocks()[1]);
    }
    SECTION("block duplicates") {
        db::FileInfo db_file;
        db_file.set_sequence(db_file_11.sequence());
        db_file.set_name("zzz");
        db_file.set_size(1024);
        db_file.add_blocks_keys(b3->get_db_key());
        db_file.add_blocks_keys(b3->get_db_key());
        db_file.add_blocks_keys(b3->get_db_key());
        auto target = model::file_info_ptr_t(new model::file_info_t(db_file, fi1_my.get()));
    }
}

TEST_CASE("blocks deleteion", "[model]") {
    db::Device db_d1;
    db_d1.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
    db_d1.set_name("d1");
    db_d1.set_cert_name("d1_cert_name");
    auto d1 = model::device_ptr_t(new model::device_t(db_d1, 2));

    db::Folder db_f;
    db_f.set_id("2");
    db_f.set_label("f2-label");
    db_f.set_path("/some/path/d2");
    auto folder = model::folder_ptr_t(new model::folder_t(db_f, 1));

    cluster_ptr_t cluster = new cluster_t(d1);
    auto folders = model::folders_map_t();
    folders.put(folder);
    cluster->assign_folders(std::move(folders));
    folder->assign_cluster(cluster.get());
    folder->assign_device(d1);

    db::FolderInfo db_folderinfo;
    auto folder_info = folder_info_ptr_t(new folder_info_t(db_folderinfo, d1.get(), folder.get(), 3));

    SECTION("deleted blocks from model") {
        db::BlockInfo db_block;
        db_block.set_hash("h1");
        db_block.set_size(5);
        db_block.set_weak_hash(23u);
        auto block = model::block_info_ptr_t(new model::block_info_t(db_block, 1));
        cluster->get_blocks().put(block);
        REQUIRE(!block->is_deleted());

        auto db_file = db::FileInfo();
        db_file.set_name("my-file.txt");
        db_file.set_sequence(5);
        db_file.set_type(proto::FileInfoType::FILE);
        db_file.mutable_blocks_keys()->Add(1);
        auto file = model::file_info_ptr_t(new model::file_info_t(db_file, folder_info.get()));
        REQUIRE(file->get_blocks().front() == block);
        proto::FileInfo pr_file;
        pr_file.set_name("my-file.txt");
        pr_file.set_sequence(15);
        pr_file.set_type(proto::FileInfoType::FILE);
        pr_file.set_deleted(true);

        auto &bm = cluster->get_blocks();
        auto &dm = cluster->get_deleted_blocks();

        REQUIRE(!dm.by_id(block->get_hash()));
        file->update(pr_file);
        CHECK(dm.size() == 1);
        CHECK(file->is_deleted());
        CHECK(file->get_blocks().size() == 0);
        CHECK(block->is_deleted());
        CHECK(dm.by_id(block->get_hash()));
        CHECK(!bm.by_id(block->get_hash()));
    }
}
