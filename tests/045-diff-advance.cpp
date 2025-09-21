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
    auto &blocks_map = cluster->get_blocks();
    auto sequencer = make_sequencer(4);
    auto &devices = cluster->get_devices();
    devices.put(my_device);
    devices.put(peer_device);

    proto::FileInfo pr_fi;
    proto::set_name(pr_fi, "a.txt");
    proto::set_size(pr_fi, 5);
    auto &v = proto::get_version(pr_fi);
    proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 5));
    auto &b_1 = proto::add_blocks(pr_fi);
    proto::set_size(b_1, 5);
    proto::set_hash(b_1, utils::sha256_digest(as_bytes("12345")).value());

    auto b_2 = proto::BlockInfo();
    proto::set_size(b_2, 5);
    proto::set_hash(b_2, utils::sha256_digest(as_bytes("67890")).value());

    auto bi_1 = block_info_t::create(b_1).assume_value();
    blocks_map.put(bi_1);

    auto bi_2 = block_info_t::create(b_2).assume_value();
    blocks_map.put(bi_2);

    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("f1", "some/path-1", "my-label-1").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "f1").apply());

    auto folder = cluster->get_folders().by_id("f1");
    auto folder_peer = folder->get_folder_infos().by_device(*peer_device);
    auto folder_my = folder->get_folder_infos().by_device(*my_device);
    auto &files_my = folder_my->get_file_infos();
    auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_fi, folder_peer).value();
    file_peer->assign_block(bi_1, 0);
    folder_peer->add_relaxed(file_peer);

    SECTION("local update") {
        SECTION("content changes") {
            auto pr_local = pr_fi;
            SECTION("new content") {
                proto::clear_blocks(pr_local);
                proto::add_blocks(pr_local) = b_2;
                REQUIRE(builder.local_update(folder->get_id(), pr_local).apply());
                REQUIRE(files_my.size() == 1);
                auto &f_my = *files_my.begin();
                CHECK(f_my->get_blocks()[0] == bi_2);
                CHECK(f_my->get_version().as_proto() != file_peer->get_version().as_proto());
            }
            SECTION("identical content (aka importing)") {
                REQUIRE(builder.local_update(folder->get_id(), pr_local).apply());
                REQUIRE(files_my.size() == 1);
                auto &f_my = *files_my.begin();
                CHECK(f_my->get_blocks()[0] == bi_1);
                CHECK(f_my->get_version().as_proto() == file_peer->get_version().as_proto());
            }
            SECTION("after suspending") {
                REQUIRE(builder.suspend(*folder).local_update(folder->get_id(), pr_local).apply());
                REQUIRE(files_my.size() == 0);
            }
            SECTION("after removing") {
                REQUIRE(builder.remove_folder(*folder).local_update(folder->get_id(), pr_local).apply());
                REQUIRE(files_my.size() == 0);
            }
        }
    }

    SECTION("remote copy") {
        SECTION("all ok") {
            REQUIRE(builder.remote_copy(*file_peer, *folder_peer).apply());
            REQUIRE(files_my.size() == 1);
        }
        SECTION("after unsharing") {
            REQUIRE(builder.unshare_folder(*folder_peer).remote_copy(*file_peer, *folder_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
        SECTION("after suspending") {
            REQUIRE(builder.suspend(*folder).remote_copy(*file_peer, *folder_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
        SECTION("after removing") {
            REQUIRE(builder.remove_folder(*folder).remote_copy(*file_peer, *folder_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
    }

    SECTION("remote win") {
        auto pr_fi_local = pr_fi;
        auto &v_l = proto::get_version(pr_fi_local);
        auto &c_l = proto::get_counters(v_l, 0);
        proto::set_value(c_l, 3);

        SECTION("all ok") {
            REQUIRE(builder.advance(*file_peer, *folder_peer).apply());
            REQUIRE(files_my.size() == 1);
        }
        SECTION("after unsharing") {
            REQUIRE(builder.unshare_folder(*folder_peer).advance(*file_peer, *folder_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
        SECTION("after suspending") {
            REQUIRE(builder.suspend(*folder).advance(*file_peer, *folder_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
        SECTION("after removing") {
            REQUIRE(builder.remove_folder(*folder).advance(*file_peer, *folder_peer).apply());
            REQUIRE(files_my.size() == 0);
        }
    }
}
