#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/file_iterator.h"

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
}
