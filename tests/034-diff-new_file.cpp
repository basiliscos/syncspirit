#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/new_file.h"
#include "model/diff/diff_visitor.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;


TEST_CASE("new file diff", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device =  device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
    REQUIRE(diff->apply(*cluster));


    proto::FileInfo pr_file_info;
    pr_file_info.set_name("a.txt");

    SECTION("symlink, inc sequence, no blocks") {
        pr_file_info.set_type(proto::FileInfoType::SYMLINK);
        pr_file_info.set_symlink_target("/some/where");
        diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(db_folder.id(), pr_file_info, {}));
        REQUIRE(diff->apply(*cluster));

        auto folder_info = cluster->get_folders().by_id(db_folder.id())->get_folder_infos().by_device(my_device);
        auto file = folder_info->get_file_infos().by_name(pr_file_info.name());
        REQUIRE(file);
        REQUIRE(file->get_name() == "a.txt");
        REQUIRE(file->get_link_target() == "/some/where");
        REQUIRE(file->is_link());
        REQUIRE(file->get_sequence() == 1);
        REQUIRE(folder_info->get_max_sequence() == 1);
    }

    SECTION("file, no inc, with blocks") {
        pr_file_info.set_type(proto::FileInfoType::FILE);
        pr_file_info.set_size(5);

        auto bi = proto::BlockInfo();
        bi.set_size(5);
        bi.set_weak_hash(12);
        bi.set_hash(utils::sha256_digest("12345").value());
        bi.set_offset(0);
        diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(db_folder.id(), pr_file_info, {bi}));
        REQUIRE(diff->apply(*cluster));

        auto folder_info = cluster->get_folders().by_id(db_folder.id())->get_folder_infos().by_device(my_device);
        auto file = folder_info->get_file_infos().by_name(pr_file_info.name());
        REQUIRE(file);
        REQUIRE(file->get_size() == 5);
        REQUIRE(file->get_name() == "a.txt");
        REQUIRE(!file->is_link());
        REQUIRE(file->get_sequence() == 1);
        REQUIRE(folder_info->get_max_sequence() == 1);
        REQUIRE(file->get_blocks().size() == 1);
        REQUIRE(file->get_blocks()[0]->get_hash() == bi.hash());
        REQUIRE(cluster->get_blocks().size() == 1);
        REQUIRE(cluster->get_blocks().get(bi.hash()));
    }
}
