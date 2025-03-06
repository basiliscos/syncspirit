// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "diff-builder.h"
#include "model/cluster.h"
#include "model/misc/sequencer.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("cluster modifications from ui", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto &folders = cluster->get_folders();
    auto id = std::string("1234-5678");
    auto label = std::string("my-label");
    auto path = std::string("/my/path");
    auto builder = diff_builder_t(*cluster);

    SECTION("folder create & update") {
        REQUIRE(builder.upsert_folder(id, path, label).apply());
        auto folder = folders.by_id(id);
        REQUIRE(folder);
        CHECK(folder->get_id() == id);
        CHECK(folder->get_label() == label);
        CHECK(folder->get_path() == path);
        CHECK(folder->get_cluster() == cluster);

        auto fi = folder->get_folder_infos().by_device(*my_device);
        REQUIRE(fi);
        CHECK(fi->get_max_sequence() == 0);
        CHECK(fi->get_index() != 0);

        REQUIRE(builder.upsert_folder(id, path, "label-2").apply());
        CHECK(folder->get_label() == "label-2");
    }

    SECTION("share folder") {
        REQUIRE(builder.upsert_folder(id, path, label).apply());

        SECTION("w/o unknown folder") {
            REQUIRE(builder.share_folder(peer_id.get_sha256(), id).apply());
            auto folder = folders.by_id(id);
            REQUIRE(folder);
            auto fi_peer = folder->get_folder_infos().by_device(*peer_device);
            REQUIRE(fi_peer);
            CHECK(fi_peer->get_device() == peer_device);
            CHECK(fi_peer->get_max_sequence() == 0);
        }

        SECTION("with unknown folder, then unshare") {
            auto db_pf = db::PendingFolder();
            auto& db_folder = db::get_folder(db_pf);
            db::set_id(db_folder, id);
            db::set_label(db_folder, label);
            db::set_path(db_folder, path);

            auto& db_fi = db::get_folder_info(db_pf);
            db::set_max_sequence(db_fi, 2345);
            db::set_index_id(db_fi, 12);

            auto uf = pending_folder_t::create(sequencer->next_uuid(), db_pf, peer_device->device_id()).value();
            cluster->get_pending_folders().put(uf);

            REQUIRE(builder.share_folder(peer_id.get_sha256(), id).apply());

            auto folder = folders.by_id(id);
            REQUIRE(folder);
            REQUIRE(folder->is_shared_with(*peer_device));
            auto fi_peer = folder->get_folder_infos().by_device(*peer_device);
            auto fi_my = folder->get_folder_infos().by_device(*my_device);
            REQUIRE(fi_my);
            REQUIRE(fi_peer);

            CHECK(fi_peer->get_device() == peer_device);
            CHECK(fi_peer->get_max_sequence() == 0);
            CHECK(fi_peer->get_index() == db::get_index_id(db_fi));
            CHECK(cluster->get_pending_folders().size() == 0);

            auto pr_file_1 = proto::FileInfo();
            proto::set_name(pr_file_1, "a.txt");
            proto::set_sequence(pr_file_1, 1l);
            proto::set_size(pr_file_1, 10);
            auto& pr_version = proto::get_version(pr_file_1);
            proto::add_counters(pr_version, proto::Counter(peer_device->device_id().get_uint(), 0));

            auto& b1 = proto::add_blocks(pr_file_1);
            proto::set_size(b1, 5);
            proto::set_hash(b1, utils::sha256_digest(as_bytes("12345")).value());
            auto bi_1 = block_info_t::create(b1).value();

            auto& b2 = proto::add_blocks(pr_file_1);
            proto::set_size(b2, 5);
            proto::set_hash(b2, utils::sha256_digest(as_bytes("567890")).value());
            auto bi_2 = block_info_t::create(b2).value();

            auto &blocks = cluster->get_blocks();
            blocks.put(bi_1);
            blocks.put(bi_2);

            auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file_1, fi_peer).value();
            fi_peer->add_strict(file_peer);
            file_peer->assign_block(bi_1, 0);
            file_peer->assign_block(bi_2, 1);

            auto file_my = file_info_t::create(sequencer->next_uuid(), pr_file_1, fi_my).value();
            fi_my->add_strict(file_my);
            file_my->assign_block(bi_2, 1);

            REQUIRE(builder.unshare_folder(*fi_peer).apply());
            REQUIRE(!folder->get_folder_infos().by_device(*peer_device));
            REQUIRE(cluster->get_blocks().size() == 1);
            REQUIRE(!folder->is_shared_with(*peer_device));
        }
    }

    SECTION("update peer") {
        REQUIRE(builder.update_peer(my_id, "myyy-devices", "cn2").apply());
        CHECK(my_device->get_name() == "myyy-devices");
        CHECK(my_device->get_cert_name() == "cn2");
    }
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
