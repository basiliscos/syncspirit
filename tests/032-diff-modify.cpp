// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/unshare_folder.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/error_code.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

template <typename F> struct my_cluster_update_visitor_t : diff::cluster_visitor_t {
    F fn;

    my_cluster_update_visitor_t(F &&fn_) : fn{std::forward<F>(fn_)} {}
    outcome::result<void> operator()(const diff::peer::cluster_update_t &diff) noexcept override { return fn(diff); }
};

TEST_CASE("cluster modifications from ui", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto &folders = cluster->get_folders();
    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");

    SECTION("folder creation") {
        auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
        REQUIRE(diff->apply(*cluster));
        auto folder = folders.by_id(db_folder.id());
        REQUIRE(folder);
        CHECK(folder->get_id() == db_folder.id());
        CHECK(folder->get_label() == db_folder.label());
        CHECK(folder->get_path() == db_folder.path());
        CHECK(folder->get_cluster() == cluster);

        auto fi = folder->get_folder_infos().by_device(*my_device);
        REQUIRE(fi);
        CHECK(fi->get_max_sequence() == 0);
        CHECK(fi->get_index() != 0);
    }

    SECTION("share folder (w/o unknown folder)") {
        auto diff_create = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
        REQUIRE(diff_create->apply(*cluster));

        SECTION("w/o unknown folder") {
            auto diff_share =
                diff::cluster_diff_ptr_t(new diff::modify::share_folder_t(peer_id.get_sha256(), db_folder.id()));
            REQUIRE(diff_share->apply(*cluster));

            auto folder = folders.by_id(db_folder.id());
            REQUIRE(folder);
            auto fi_peer = folder->get_folder_infos().by_device(*peer_device);
            REQUIRE(fi_peer);
            CHECK(fi_peer->get_device() == peer_device);
            CHECK(fi_peer->get_max_sequence() == 0);
        }

        SECTION("with unknown folder, then unshare") {
            auto db_uf = db::UnknownFolder();
            *db_uf.mutable_folder() = db_folder;
            auto db_fi = db_uf.mutable_folder_info();
            db_fi->set_index_id(2345);
            db_fi->set_max_sequence(12);

            auto uf = unknown_folder_t::create(cluster->next_uuid(), db_uf, peer_device->device_id()).value();
            cluster->get_unknown_folders().emplace_front(uf);

            auto diff_share =
                diff::cluster_diff_ptr_t(new diff::modify::share_folder_t(peer_id.get_sha256(), db_folder.id()));
            REQUIRE(diff_share->apply(*cluster));

            auto folder = folders.by_id(db_folder.id());
            REQUIRE(folder);
            auto fi_peer = folder->get_folder_infos().by_device(*peer_device);
            REQUIRE(fi_peer);
            CHECK(fi_peer->get_device() == peer_device);
            CHECK(fi_peer->get_max_sequence() == 12);
            CHECK(fi_peer->get_index() == 2345);
            CHECK(cluster->get_unknown_folders().empty());

            auto diff_unshare =
                diff::cluster_diff_ptr_t(new diff::modify::unshare_folder_t(peer_id.get_sha256(), db_folder.id()));
            REQUIRE(diff_unshare->apply(*cluster));
            REQUIRE(!folder->get_folder_infos().by_device(*peer_device));
        }
    }

    SECTION("update peer") {
        db::Device db;
        db.set_name("myyy-devices");
        db.set_cert_name("cn2");
        auto diff = diff::cluster_diff_ptr_t(new diff::modify::update_peer_t(db, my_id.get_sha256()));
        REQUIRE(diff->apply(*cluster));
        CHECK(my_device->get_name() == "myyy-devices");
        CHECK(my_device->get_cert_name() == "cn2");
    }

    SECTION("update peer // wrong decice_id") {
        db::Device db;
        db.set_name("myyy-devices");
        db.set_cert_name("cn2");
        auto diff = diff::cluster_diff_ptr_t(new diff::modify::update_peer_t(db, "wrong-sha256"));
        auto r = diff->apply(*cluster);
        REQUIRE(!r);
        auto err = r.error();
        CHECK(err == error_code_t::malformed_deviceid);
    }
}
