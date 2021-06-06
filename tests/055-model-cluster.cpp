#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

namespace bfs = boost::filesystem;

TEST_CASE("opt_for_synch", "[model]") {
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

    bfs::path root("some/root");
    bfs::path path("some/path");

    auto add_file = [&key](std::int64_t seq, model::device_ptr_t device, model::folder_ptr_t folder) mutable {
      db::FolderInfo db_folder_info;
      db_folder_info.set_max_sequence(seq);
      auto folder_info = model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, device.get(), folder.get(), ++key));
      folder->add(folder_info);

      db::FileInfo db_file_info;
      db_file_info.set_sequence(seq);
      auto file_info = model::file_info_ptr_t(new model::file_info_t(db_file_info, folder.get()));
      folder->add(file_info);
      return std::tuple(folder_info, file_info);
    };

    SECTION("no need of update") {
        add_file(5, d1, f1);
        add_file(4, d2, f1);
        add_file(5, d1, f2);
        add_file(4, d2, f2);
        auto f = cluster->folder_for_synch(d2);
        CHECK(!f);
    }

    SECTION("f1 & f2 needs update, but f1 has higher score") {
        add_file(5, d1, f1);
        add_file(9, d2, f1);
        add_file(5, d1, f2);
        add_file(8, d2, f2);
        auto f = cluster->folder_for_synch(d2);
        CHECK(f == f1);
    }

    SECTION("f1 & f2 needs update, but f2 has higher score") {
        add_file(5, d1, f1);
        add_file(10, d2, f1);
        add_file(5, d1, f2);
        add_file(18, d2, f2);
        auto f = cluster->folder_for_synch(d2);
        CHECK(*f == *f2);
    }

    SECTION("local updates") {
        auto [folder_info, file_info] = add_file(5, d1, f1);
        add_file(5, d2, f1);
        SECTION("empty emtpy local file map") {
            local_file_map_t lfm(root);
            SECTION("emtpy local file map") {
                f1->update(lfm);
                CHECK(file_info->is_outdated());
                auto r = cluster->file_for_synch(d2);
                CHECK(r);
                CHECK(*r == *file_info);
            }
        }
    }
}
