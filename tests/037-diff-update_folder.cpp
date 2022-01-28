#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/peer/update_folder.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;


TEST_CASE("update folder (via Index)", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device =  device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device =  device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto& folders = cluster->get_folders();
    db::Folder db_folder_1;
    db_folder_1.set_id("1234-5678");
    db_folder_1.set_label("my-label");
    db_folder_1.set_path("/my/path");

    db::Folder db_folder_2;
    db_folder_2.set_id("5555-4444");
    db_folder_2.set_label("my-l2");
    db_folder_2.set_path("/my/path/2");

    auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder_1));
    REQUIRE(diff->apply(*cluster));

    diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder_2));
    REQUIRE(diff->apply(*cluster));

    auto folder = folders.by_id(db_folder_1.id());
    diff = diff::cluster_diff_ptr_t(new diff::modify::share_folder_t(peer_id.get_sha256(), db_folder_1.id()));
    REQUIRE(diff->apply(*cluster));

    auto pr_index = proto::Index();
    pr_index.set_folder(db_folder_1.id());

    SECTION("successful case") {
        auto file = pr_index.add_files();
        file->set_name("a.txt");
        file->set_sequence(10ul);
        file->set_size(5ul);
        file->set_block_size(5ul);
        file->set_modified_s(1);
        auto b = file->add_blocks();
        b->set_hash("123");

        diff = diff::peer::update_folder_t::create(*cluster, *peer_device, pr_index).value();
        REQUIRE(diff->apply(*cluster));

        auto peer_folder_info = folder->get_folder_infos().by_device(peer_device);
        auto& peer_files = peer_folder_info->get_file_infos();
        REQUIRE(peer_files.size() == 1);
        CHECK(peer_folder_info->is_actual());
        auto f = peer_files.by_name(file->name());
        REQUIRE(f);

        auto key = std::string(f->get_key());

        SECTION("when a file with existing name is added, key is kept") {
            auto index_update = proto::IndexUpdate{};
            index_update.set_folder(pr_index.folder());
            auto same_file = index_update.add_files();
            *same_file = *file;
            same_file->set_modified_s(2);
            same_file->set_sequence(11ul);

            diff = diff::peer::update_folder_t::create(*cluster, *peer_device, index_update).value();
            REQUIRE(diff->apply(*cluster));

            REQUIRE(peer_files.size() == 1);
            auto same_f = peer_files.by_name(file->name());
            CHECK(same_f.get() != f.get());
            CHECK(same_f->get_key() == f->get_key());
        }
    }

    SECTION("folder does not exists") {
        pr_index.set_folder(db_folder_1.id() + "xxx");
        auto opt = diff::peer::update_folder_t::create(*cluster, *peer_device, pr_index);
        REQUIRE(!opt);
        CHECK(opt.error() == model::make_error_code(model::error_code_t::folder_does_not_exist));
    }

    SECTION("folder is not shared") {
        pr_index.set_folder(db_folder_2.id());
        auto opt = diff::peer::update_folder_t::create(*cluster, *peer_device, pr_index);
        REQUIRE(!opt);
        CHECK(opt.error() == model::make_error_code(model::error_code_t::folder_is_not_shared));
    }

    SECTION("no progress") {
        pr_index.set_folder(db_folder_1.id());
        auto f = pr_index.add_files();
        auto opt = diff::peer::update_folder_t::create(*cluster, *peer_device, pr_index);
        REQUIRE(!opt);
        CHECK(opt.error() == model::make_error_code(model::error_code_t::no_progress));
    }

    SECTION("blocks are not expected") {
        auto file = pr_index.add_files();
        file->set_name("a.txt");
        file->set_sequence(10ul);
        file->set_size(5ul);
        file->set_block_size(5ul);
        file->set_deleted(true);
        auto b = file->add_blocks();
        b->set_hash("123");

        auto opt = diff::peer::update_folder_t::create(*cluster, *peer_device, pr_index);
        REQUIRE(!opt);
        CHECK(opt.error() == model::make_error_code(model::error_code_t::unexpected_blocks));
    }
}

