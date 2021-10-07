#include "catch.hpp"
#include "test-utils.h"
#include "model/cluster.h"
#include "model/block_iterator.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

TEST_CASE("block iterator", "[model]") {
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
    auto fi_my = model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, d1.get(), f1.get(), ++key));
    auto fi_peer = model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, d2.get(), f1.get(), ++key));
    f1->add(fi_my);
    f1->add(fi_peer);

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

    cluster_ptr_t cluster = new cluster_t(d1);
    cluster->assign_folders(std::move(folders));

    cluster->get_blocks().put(b1);
    cluster->get_blocks().put(b2);

    SECTION("no blocks") {
        db::FileInfo db_file_1;
        auto f1 = model::file_info_ptr_t(new model::file_info_t(db_file_1, fi_my.get()));
        auto f2 = model::file_info_ptr_t(new model::file_info_t(db_file_1, fi_my.get()));
        auto it = model::blocks_interator_t(*f1, *f2);
        CHECK(!it);
    }

    SECTION("source has 2 blocks, target has 0 blocks") {
        db::FileInfo db_file;
        db_file.set_block_size(5);
        db_file.set_sequence(5);
        db_file.add_blocks_keys(b1->get_db_key());
        db_file.add_blocks_keys(b2->get_db_key());
        auto f_peer = model::file_info_ptr_t(new model::file_info_t(db_file, fi_peer.get()));
        auto f_my = f_peer->link(d1);
        auto it = model::blocks_interator_t(*f_peer, *f_my);
        CHECK((bool)it);

        auto fb1 = it.next();
        CHECK(fb1);
        CHECK(fb1.block() == b1.get());
        CHECK(fb1.file() == f_peer.get());
        CHECK(fb1.block_index() == 0ul);
        CHECK(fb1.get_offset() == 0ul);
        CHECK(fb1.is_locally_available() == false);

        CHECK((bool)it);
        auto fb2 = it.next();
        CHECK(fb2);
        CHECK(fb2.block() == b2.get());
        CHECK(fb2.file() == f_peer.get());
        CHECK(fb2.block_index() == 1ul);
        fb2.get_offset();
        CHECK(fb2.get_offset() == 5ul);
        CHECK(fb2.is_locally_available() == false);

        CHECK(!it);

        it = model::blocks_interator_t(*f_peer, *f_my);
        it.reset();
        CHECK(!it);
    }

    SECTION("source has 2 blocks, target has 1 blocks") {
        db::FileInfo db_file;
        db_file.set_block_size(5);
        db_file.set_sequence(5);
        db_file.add_blocks_keys(b1->get_db_key());
        db_file.add_blocks_keys(b2->get_db_key());
        auto f_peer = model::file_info_ptr_t(new model::file_info_t(db_file, fi_peer.get()));
        auto f_my = f_peer->link(d1);
        f_my->get_blocks()[0] = b1;
        b1->link(f_my.get(), 0);
        auto it = model::blocks_interator_t(*f_peer, *f_my);
        CHECK((bool)it);

        CHECK((bool)it);
        auto fb2 = it.next();
        CHECK(fb2);
        CHECK(fb2.block() == b2.get());
        CHECK(fb2.file() == f_peer.get());
        CHECK(fb2.block_index() == 1ul);
        fb2.get_offset();
        CHECK(fb2.get_offset() == 5ul);
        CHECK(fb2.is_locally_available() == false);

        CHECK(!it);
    }
}
