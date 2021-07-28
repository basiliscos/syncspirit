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

    SECTION("no blocks") {
        proto::FileInfo info;
        auto file = file_info_t(info, folder.get());
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

        auto file = file_info_t(info, folder.get());
        auto bi = file.iterate_blocks();
        REQUIRE((bool)bi);

        SECTION("normal iteration") {
            auto r1 = bi.next();
            REQUIRE((bool)bi);
            CHECK(r1.block_index == 0);
            CHECK(r1.block->get_hash() == "h1");

            auto r2 = bi.next();
            REQUIRE(!bi);
            CHECK(r2.block_index == 1);
            CHECK(r2.block->get_hash() == "h2");
        }

        SECTION("iteration, then reset") {
            auto r1 = bi.next();
            REQUIRE((bool)bi);
            CHECK(r1.block_index == 0);
            CHECK(r1.block->get_hash() == "h1");

            bi.reset();
            REQUIRE(!bi);
        }
    }
}
