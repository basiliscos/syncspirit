#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/local_update.h"
#include "model/diff/modify/new_file.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;


TEST_CASE("various block diffs", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device =  device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    auto& blocks_map = cluster->get_blocks();

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
    REQUIRE(diff->apply(*cluster));

    auto folder_info = cluster->get_folders().by_id(db_folder.id())->get_folder_infos().by_device(my_device);
    proto::FileInfo pr_file_info;
    pr_file_info.set_name("a.txt");
    pr_file_info.set_block_size(5ul);
    pr_file_info.set_size(10ul);

    diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(db_folder.id(), pr_file_info, {}));
    REQUIRE(diff->apply(*cluster));
    auto file = folder_info->get_file_infos().by_name("a.txt");

    auto bi1 = proto::BlockInfo();
    bi1.set_size(5);
    bi1.set_weak_hash(12);
    bi1.set_hash(utils::sha256_digest("12345").value());
    bi1.set_offset(0);

    auto bi2 = proto::BlockInfo();
    bi2.set_size(5);
    bi2.set_weak_hash(12);
    bi2.set_hash(utils::sha256_digest("567890").value());
    bi2.set_offset(5ul);

    diff = diff::cluster_diff_ptr_t(new diff::modify::local_update_t(*file, file->as_db(false), {bi1, bi2}));
    REQUIRE(diff->apply(*cluster));
    REQUIRE(!file->is_locally_available());

    SECTION("append") {
        auto bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*file, 0, "12345"));
        REQUIRE(bdiff->apply(*cluster));
        auto& blocks = file->get_blocks();

        auto lf1 = blocks[0]->local_file();
        REQUIRE(lf1);
        CHECK(lf1.block_index() == 0);
        CHECK(lf1.get_offset() == 0);
        CHECK(lf1.is_locally_available());
        CHECK(!file->is_locally_available());
    }

    SECTION("clone, from different file") {
        proto::FileInfo pr_source;
        pr_source.set_name("b.txt");
        pr_source.set_block_size(5ul);
        pr_source.set_size(5ul);

        diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(db_folder.id(), pr_source, {bi2}));
        REQUIRE(diff->apply(*cluster));
        auto source = folder_info->get_file_infos().by_name("b.txt");
        auto b2 = source->get_blocks().at(0);
        b2->mark_local_available(source.get());

        auto bdiff = diff::block_diff_ptr_t(new diff::modify::clone_block_t(*file, *b2));
        REQUIRE(bdiff->apply(*cluster));
        auto& blocks = file->get_blocks();

        auto lf1 = blocks[1]->local_file();
        REQUIRE(lf1);
        CHECK(lf1.block_index() == 1);
        CHECK(lf1.get_offset() == 5);
        CHECK(lf1.is_locally_available());
        CHECK(!file->is_locally_available());
    }


}
