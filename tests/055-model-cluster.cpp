#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

namespace bfs = boost::filesystem;

TEST_CASE("iterate_files", "[model]") {
    std::uint64_t key = 0;
    db::Device db_d1;
    db_d1.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
    db_d1.set_name("d1");
    db_d1.set_cert_name("d1_cert_name");
    auto d1 = model::device_ptr_t(new model::device_t(db_d1, ++key));

    db::Device db_d2;
    db_d2.set_id(test::device_id2sha256("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7"));
    db_d2.set_name("d2");
    db_d2.set_cert_name("d2_cert_name");
    auto d2 = model::device_ptr_t(new model::device_t(db_d2, ++key));

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

    cluster_ptr_t cluster = new cluster_t(d1);
    cluster->assign_folders(std::move(folders));

    SECTION("cluster config") {
        std::int64_t seq = 1;
        db::FolderInfo db_folder_info;
        db_folder_info.set_max_sequence(++seq);

        auto fi1 = model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, d1.get(), f1.get(), ++seq));
        f1->add(fi1);

        auto fi2 = model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, d2.get(), f2.get(), ++seq));
        f1->add(fi2);

        auto config = cluster->get(d2);
        REQUIRE(config.folders_size() == 1);
        auto &f = config.folders(0);
        CHECK(f.label() == f1->label());
        CHECK(f.id() == f1->id());

        REQUIRE(f.devices_size() == 2);
        auto &sd1 = f.devices(0);
        auto &sd2 = f.devices(1);
        CHECK(((sd1.name() == d1->name) || (sd1.name() == d2->name)));
        CHECK(((sd2.name() == d1->name) || (sd2.name() == d2->name)));
    }

    SECTION("iterate files") {
        auto add_file = [&key](model::device_ptr_t device, model::folder_ptr_t folder, int64_t seq) mutable {
            db::FolderInfo db_folder_info;
            db_folder_info.set_max_sequence(seq);
            auto folder_info =
                model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, device.get(), folder.get(), ++key));
            folder->add(folder_info);

            db::FileInfo db_file_info;
            db_file_info.set_sequence(seq);
            auto file_info = model::file_info_ptr_t(new model::file_info_t(db_file_info, folder_info.get()));
            folder_info->add(file_info);
            return std::tuple(folder_info, file_info);
        };

        SECTION("no files at all") {
            auto it = cluster->iterate_files(d2);
            CHECK(!it);
        }

        SECTION("no files at peer") {
            auto it = cluster->iterate_files(d2);
            add_file(d1, f1, 1);
            CHECK(!it);
        }

        SECTION("one file, but 1 is newer") {
            auto [folder_info_1, file_info_1] = add_file(d1, f1, 1);
            auto [folder_info_2, file_info_2] = add_file(d2, f1, 2);
            auto it = cluster->iterate_files(d2);
            REQUIRE((bool)it);
            auto file = it.next();

            CHECK(file->is_locked());
            CHECK(file->get_full_name() == file_info_1->get_full_name());
            CHECK(file->get_db_key() == file_info_1->get_db_key());
            CHECK(!file->is_older(*file_info_2));
            CHECK(!file->is_older(*file_info_1));
            CHECK(!it);

            file->after_sync();
            CHECK(!file->is_locked());

            db::BlockInfo db_b1;
            db_b1.set_hash("h1");
            db_b1.set_size(5);
            db_b1.set_weak_hash(23u);
            auto b1 = model::block_info_ptr_t(new model::block_info_t(db_b1, 1));

            db::BlockInfo db_b2;
            db_b1.set_hash("h2");
            db_b1.set_size(5);
            db_b1.set_weak_hash(23u);
            auto b2 = model::block_info_ptr_t(new model::block_info_t(db_b2, 1));

            auto &blocks = file->get_blocks();
            auto &local_blocks = file->get_local_blocks();
            local_blocks.push_back(model::block_info_ptr_t());
            local_blocks.push_back(model::block_info_ptr_t());
            blocks.push_back(b1);
            blocks.push_back(b2);
            b1->link(file.get(), 0);
            b2->link(file.get(), 0);
            file->mark_local_available(0);

            it = cluster->iterate_files(d2);
            REQUIRE((bool)it);
            auto file2 = it.next();
            CHECK(file2 == file);
        }

        SECTION("when files are in sync, it is not returned") {
            add_file(d1, f1, 2);
            add_file(d2, f1, 2);
            auto it = cluster->iterate_files(d2);
            CHECK(!it);
        }
    }
}
