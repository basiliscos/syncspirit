// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "diff-builder.h"

#include "model/cluster.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("remove folder", "[model]") {
    test::init_logging();

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto my_device = device_ptr_t{};
    my_device = new model::local_device_t(my_id, "my-device", "my-device");
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    auto &devices = cluster->get_devices();
    devices.put(my_device);
    devices.put(peer_device);

    proto::FileInfo pr_fi;
    proto::set_name(pr_fi, "a.txt");
    auto& v = proto::get_version(pr_fi);
    proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 5));

    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("f1", "some/path-1", "my-label-1").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "f1").apply());

    auto folder = cluster->get_folders().by_id("f1");
    auto folder_peer = folder->get_folder_infos().by_device(*peer_device);
    auto folder_my = folder->get_folder_infos().by_device(*my_device);
    auto &files_my = folder_my->get_file_infos();
    auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_fi, folder_peer).value();

    SECTION("local update") {
        SECTION("all ok") {
            REQUIRE(builder.local_update(folder->get_id(), pr_fi).apply());
            REQUIRE(files_my.size() == 1);
        }
        SECTION("after suspending") {
            REQUIRE(builder.suspend(*folder).local_update(folder->get_id(), pr_fi).apply());
            REQUIRE(files_my.size() == 0);
        }
        SECTION("after removing") {
            REQUIRE(builder.remove_folder(*folder).local_update(folder->get_id(), pr_fi).apply());
            REQUIRE(files_my.size() == 0);
        }
    }

    SECTION("remote copy") {
        SECTION("all ok") {
            REQUIRE(builder.remote_copy(*file_peer).apply());
            REQUIRE(files_my.size() == 1);
        }
        SECTION("after unsharing") {
            REQUIRE(builder.unshare_folder(*folder_peer).remote_copy(*file_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
        SECTION("after suspending") {
            REQUIRE(builder.suspend(*folder).remote_copy(*file_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
        SECTION("after removing") {
            REQUIRE(builder.remove_folder(*folder).remote_copy(*file_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
    }

    SECTION("remote win") {
        auto pr_fi_local = pr_fi;
        auto& v_l = proto::get_version(pr_fi_local);
        auto& c_l = proto::get_counters(v_l, 0);
        proto::set_value(c_l, 3);

        SECTION("all ok") {
            REQUIRE(builder.advance(*file_peer).apply());
            REQUIRE(files_my.size() == 1);
        }
        SECTION("after unsharing") {
            REQUIRE(builder.unshare_folder(*folder_peer).advance(*file_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
        SECTION("after suspending") {
            REQUIRE(builder.suspend(*folder).advance(*file_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
        SECTION("after removing") {
            REQUIRE(builder.remove_folder(*folder).advance(*file_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
    }
}
