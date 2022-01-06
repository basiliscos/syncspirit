#include "catch.hpp"
#include "test-utils.h"
#include "model/cluster.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/new_file.h"
#include "model/diff/modify/share_folder.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

TEST_CASE("file_info_t::need_download", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto my_device =  device_t::create(my_id, "my-device").value();
    auto peer_device =  device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto& folders = cluster->get_folders();

    auto db_folder = db::Folder();
    auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
    REQUIRE(diff->apply(*cluster));
    diff = new diff::modify::share_folder_t(peer_id.get_sha256(), db_folder.id());
    REQUIRE(diff->apply(*cluster));

    auto folder = folders.by_id(db_folder.id());
    auto& folder_infos = folder->get_folder_infos();
    auto folder_my = folder_infos.by_device(my_device);
    auto folder_peer = folder_infos.by_device(peer_device);

    auto pr_file = proto::FileInfo();
    pr_file.set_name("a.txt");

    SECTION("file is empty => no download") {
        auto file_my = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
        auto file_peer = file_info_t::create(cluster->next_uuid(), pr_file, folder_peer).value();
        CHECK(!file_my->need_download(*file_peer));
    }

    SECTION("locked file => no download") {
        auto file_my = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
        auto file_peer = file_info_t::create(cluster->next_uuid(), pr_file, folder_peer).value();
        file_my->lock();
        CHECK(!file_my->need_download(*file_peer));
    }

    pr_file.set_block_size(5);
    pr_file.set_size(5);
    auto* peer_v = pr_file.mutable_version();
    auto* peer_c1= peer_v->add_counters();
    peer_c1->set_id(1);
    peer_c1->set_value(1);

    auto b = pr_file.add_blocks();
    b->set_hash(utils::sha256_digest("12345").value());
    b->set_weak_hash(555);
    auto bi = block_info_t::create(*b).value();
    auto& blocks_map = cluster->get_blocks();
    blocks_map.put(bi);
    auto bbb = blocks_map.get(b->hash());
    REQUIRE(bbb);

    diff = new diff::modify::new_file_t(*cluster, db_folder.id(), pr_file, {*b});
    REQUIRE(diff->apply(*cluster));
    auto file_my = folder_my->get_file_infos().by_name(pr_file.name());


    SECTION("versions are identical, no local file => download") {
        auto file_peer = file_info_t::create(cluster->next_uuid(), pr_file, folder_peer).value();
        CHECK(file_my->need_download(*file_peer));
    }

    SECTION("versions are identical, already available => no download") {
        auto file_peer = file_info_t::create(cluster->next_uuid(), pr_file, folder_peer).value();
        file_my->mark_local_available(0);
        REQUIRE(file_my->is_locally_available());
        CHECK(!file_my->need_download(*file_peer));
    }

    SECTION("peer's version is newer => download") {
        auto* peer_c2= peer_v->add_counters();
        peer_c1->set_id(1);
        peer_c1->set_value(2);
        auto file_peer = file_info_t::create(cluster->next_uuid(), pr_file, folder_peer).value();

        file_my->mark_local_available(0);
        CHECK(file_my->need_download(*file_peer));
    }

    SECTION("peer's counter is newer => download") {
        peer_c1->set_value(2);
        auto file_peer = file_info_t::create(cluster->next_uuid(), pr_file, folder_peer).value();

        file_my->mark_local_available(0);
        CHECK(file_my->need_download(*file_peer));
    }

}
