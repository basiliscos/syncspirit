#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/lock_file.h"
#include "model/diff/modify/new_file.h"
#include "model/diff/peer/peer_state.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;


TEST_CASE("peer state update", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device =  device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device =  device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    rotor::address_ptr_t addr;
    auto diff = diff::cluster_diff_ptr_t(new diff::peer::peer_state_t(peer_id.get_sha256(), addr, true));
    CHECK(peer_device->is_online() == false);

    REQUIRE(diff->apply(*cluster));
    CHECK(peer_device->is_online() == true);

    diff = new diff::peer::peer_state_t(peer_id.get_sha256(), addr, false);
    REQUIRE(diff->apply(*cluster));
    CHECK(peer_device->is_online() == false);
}

TEST_CASE("lock file", "[model]") {
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
    pr_file_info.set_type(proto::FileInfoType::SYMLINK);
    pr_file_info.set_symlink_target("/some/where");
    diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(db_folder.id(), pr_file_info, {}));
    REQUIRE(diff->apply(*cluster));

    diff = diff::cluster_diff_ptr_t(new diff::modify::lock_file_t(db_folder.id(), pr_file_info.name(), true));
    REQUIRE(diff->apply(*cluster));
    auto folder_info = cluster->get_folders().by_id(db_folder.id())->get_folder_infos().by_device(my_device);
    auto file = folder_info->get_file_infos().by_name(pr_file_info.name());
    REQUIRE(file->is_locked());

    diff = diff::cluster_diff_ptr_t(new diff::modify::lock_file_t(db_folder.id(), pr_file_info.name(), false));
    REQUIRE(diff->apply(*cluster));
    REQUIRE(!file->is_locked());
}
