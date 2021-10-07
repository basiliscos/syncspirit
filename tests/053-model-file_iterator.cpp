#include "catch.hpp"
#include "test-utils.h"
#include "model/cluster.h"
#include "model/file_iterator.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

TEST_CASE("file iterator", "[model]") {
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

    SECTION("no files") {
        auto it = cluster->iterate_files(d2);
        CHECK(!it);
    }

    SECTION("1 source and absent target") {
        db::FileInfo db_file_11;
        db_file_11.set_sequence(++key);
        db_file_11.set_name("zzz");
        db_file_11.set_size(1024);
        auto file_source = model::file_info_ptr_t(new model::file_info_t(db_file_11, fi1_peer.get()));
        fi1_peer->add(file_source);

        auto it = cluster->iterate_files(d2);
        CHECK((bool)it);
        auto source = it.next();
        CHECK(source == file_source);
        CHECK(!it);

        it = cluster->iterate_files(d2);
        it.reset();
        CHECK(!it);
    }

    SECTION("1 source and target is outdated") {
        db::FileInfo db_file_11;
        db_file_11.set_sequence(++key);
        db_file_11.set_name("zzz");
        db_file_11.set_size(1024);
        auto file_source = model::file_info_ptr_t(new model::file_info_t(db_file_11, fi1_peer.get()));
        fi1_peer->add(file_source);

        db::FileInfo db_file_12;
        db_file_12.set_sequence(file_source->get_sequence() - 1);
        db_file_12.set_name("zzz");
        db_file_12.set_size(1024);
        auto file_target = model::file_info_ptr_t(new model::file_info_t(db_file_12, fi1_peer.get()));
        fi1_my->add(file_target);

        auto it = cluster->iterate_files(d2);
        CHECK((bool)it);
        auto source = it.next();
        CHECK(source == file_source);
        CHECK(!it);

        SECTION("locked file is skipped") {
            file_target->lock();
            auto it = cluster->iterate_files(d2);
            CHECK(!it);
        }
    }

    SECTION("1 source is outdated and target") {
        db::FileInfo db_file_11;
        db_file_11.set_sequence(++key);
        db_file_11.set_name("zzz");
        db_file_11.set_size(1024);
        auto file_source = model::file_info_ptr_t(new model::file_info_t(db_file_11, fi1_peer.get()));
        fi1_peer->add(file_source);

        db::FileInfo db_file_12;
        db_file_12.set_sequence(file_source->get_sequence() + 1);
        db_file_12.set_name("zzz");
        db_file_12.set_size(1024);
        auto file_target = model::file_info_ptr_t(new model::file_info_t(db_file_12, fi1_peer.get()));
        fi1_my->add(file_target);

        auto it = cluster->iterate_files(d2);
        CHECK(!it);
    }

    SECTION("1 source iand incomplete target") {
        db::FileInfo db_file_11;
        db_file_11.set_sequence(++key);
        db_file_11.set_name("zzz");
        db_file_11.set_size(1024);
        auto file_source = model::file_info_ptr_t(new model::file_info_t(db_file_11, fi1_peer.get()));
        fi1_peer->add(file_source);

        db::FileInfo db_file_12;
        db_file_12.set_sequence(file_source->get_sequence());
        db_file_12.set_name("zzz");
        db_file_12.set_size(1024);
        auto file_target = model::file_info_ptr_t(new model::file_info_t(db_file_12, fi1_peer.get()));
        fi1_my->add(file_target);
        file_target->mark_incomplete();

        auto it = cluster->iterate_files(d2);
        CHECK((bool)it);
        auto source = it.next();
        CHECK(source == file_source);
        CHECK(!it);
    }
}
