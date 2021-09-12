#include "catch.hpp"
#include "test-utils.h"
#include "model/file_info.h"
#include "model/cluster.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

namespace bfs = boost::filesystem;

TEST_CASE("block iterator", "[model]") {
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

    SECTION("no blocks") {
        proto::FileInfo info;
        auto file = file_info_t(info, folder_info.get());
        auto bi = file.iterate_blocks();
        REQUIRE(!bi);
    }

    SECTION("a few remote blocks") {
        proto::BlockInfo b1;
        b1.set_hash("h1");
        b1.set_size(1024);

        proto::BlockInfo b2;
        b2.set_hash("h2");
        b2.set_size(9);

        proto::FileInfo info;
        *info.add_blocks() = b1;
        *info.add_blocks() = b2;

        auto file = file_info_t(info, folder_info.get());

        SECTION("normal iteration") {
            auto bi = file.iterate_blocks();
            REQUIRE((bool)bi);

            auto r1 = bi.next();
            REQUIRE((bool)bi);
            CHECK(r1.block_index == 0);
            CHECK(r1.block->get_hash() == "h1");

            auto r2 = bi.next();
            REQUIRE(!bi);
            CHECK(r2.block_index == 1);
            CHECK(r2.block->get_hash() == "h2");

            REQUIRE(!bi);
        }

        SECTION("a block is locally available") {
            file.mark_local_available(0);
            auto bi = file.iterate_blocks();
            REQUIRE((bool)bi);

            auto r1 = bi.next();
            REQUIRE((bool)bi);
            CHECK(r1.block_index == 0);
            CHECK(r1.block->get_hash() == "h1");
            CHECK(r1.block->local_file().file_info == &file);

            auto r2 = bi.next();
            REQUIRE(!bi);
            CHECK(r2.block_index == 1);
            CHECK(r2.block->get_hash() == "h2");

            REQUIRE(!bi);
        }

        SECTION("iteration, then reset") {
            auto bi = file.iterate_blocks();
            REQUIRE((bool)bi);

            auto r1 = bi.next();
            REQUIRE((bool)bi);
            CHECK(r1.block_index == 0);
            CHECK(r1.block->get_hash() == "h1");

            bi.reset();
            REQUIRE(!bi);
        }
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
        CHECK(file->is_deleted());
        CHECK(file->get_blocks().size() == 0);
        CHECK(block->is_deleted());
        CHECK(dm.by_id(block->get_hash()));
        CHECK(!bm.by_id(block->get_hash()));
    }
}
