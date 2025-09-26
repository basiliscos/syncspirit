// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/misc/sequencer.h"
#include "diff-builder.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/cluster_visitor.h"
#include "model/diff/cluster_diff.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/diff/modify/remove_files.h"
#include "model/diff/modify/remove_folder_infos.h"
#include "model/diff/modify/remove_pending_folders.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/modify/add_pending_folders.h"
#include "utils/error_code.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

template <typename F> struct my_cluster_update_visitor_t : diff::cluster_visitor_t {
    F fn;
    int remove_blocks = 0;
    int remove_files = 0;
    int remove_folders = 0;
    int remove_pending_folders = 0;
    int add_pending_folders = 0;
    int updated_folders = 0;

    my_cluster_update_visitor_t(F &&fn_) : fn{std::forward<F>(fn_)} {}

    outcome::result<void> operator()(const diff::peer::cluster_update_t &diff, void *custom) noexcept override {
        fn(diff);
        return diff.visit_next(*this, custom);
    }

    outcome::result<void> operator()(const diff::modify::remove_blocks_t &diff, void *custom) noexcept override {
        ++remove_blocks;
        return diff.visit_next(*this, custom);
    }
    outcome::result<void> operator()(const diff::modify::remove_files_t &diff, void *custom) noexcept override {
        ++remove_files;
        return diff.visit_next(*this, custom);
    }
    outcome::result<void> operator()(const diff::modify::remove_folder_infos_t &diff, void *custom) noexcept override {
        ++remove_folders;
        return diff.visit_next(*this, custom);
    }
    outcome::result<void> operator()(const diff::modify::remove_pending_folders_t &diff,
                                     void *custom) noexcept override {
        ++remove_pending_folders;
        return diff.visit_next(*this, custom);
    }
    outcome::result<void> operator()(const diff::modify::upsert_folder_info_t &diff, void *custom) noexcept override {
        ++updated_folders;
        return diff.visit_next(*this, custom);
    }
    outcome::result<void> operator()(const diff::modify::add_pending_folders_t &diff, void *custom) noexcept override {
        ++add_pending_folders;
        return diff.visit_next(*this, custom);
    }
};

TEST_CASE("cluster update, new folder", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    SECTION("unknown folder") {
        auto cc = std::make_unique<proto::ClusterConfig>();
        auto &pr_folder = proto::add_folders(*cc);
        proto::set_id(pr_folder, "some-id");
        proto::set_label(pr_folder, "some-label");

        auto &pr_peer = proto::add_devices(pr_folder);
        proto::set_id(pr_peer, peer_id.get_sha256());
        proto::set_name(pr_peer, peer_device->get_name());
        proto::set_max_sequence(pr_peer, 10);
        proto::set_index_id(pr_peer, 22u);

        auto diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
        REQUIRE(diff_opt);

        auto &diff = diff_opt.value();
        auto r_a = diff->apply(*controller, {});
        CHECK(r_a);

        auto visitor = my_cluster_update_visitor_t([&](auto &) {});
        auto r_v = diff->visit(visitor, nullptr);
        REQUIRE(r_v);
        REQUIRE(cluster->get_pending_folders().size());
        REQUIRE(visitor.add_pending_folders == 1);
        auto uf = cluster->get_pending_folders().begin()->item;
        CHECK(uf->device_id() == peer_device->device_id());
        CHECK(uf->get_id() == "some-id");
        CHECK(uf->get_max_sequence() == 10);
        CHECK(uf->get_index() == 22ul);

        // no changes
        db::PendingFolder db_pf;
        auto &mf = db::get_folder(db_pf);
        db::set_id(mf, proto::get_id(pr_folder));
        auto &mfi = db::get_folder_info(db_pf);
        db::set_max_sequence(mfi, proto::get_max_sequence(pr_peer));
        db::set_index_id(mfi, proto::get_index_id(pr_peer));

        diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
        REQUIRE(diff_opt);
        diff = diff_opt.value();
        r_a = diff->apply(*controller, {});
        CHECK(r_a);
        REQUIRE(cluster->get_pending_folders().size() == 1);
        std::ignore = diff->visit(visitor, nullptr);
        REQUIRE(visitor.add_pending_folders == 1);

        // max-id changed
        proto::set_max_sequence(pr_peer, 15);
        diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
        REQUIRE(diff_opt);
        diff = diff_opt.value();
        r_a = diff->apply(*controller, {});
        CHECK(r_a);
        (void)diff->visit(visitor, nullptr);
        REQUIRE(visitor.add_pending_folders == 2);
        REQUIRE(cluster->get_pending_folders().size() == 1);
        uf = cluster->get_pending_folders().begin()->item;
        CHECK(uf->device_id() == peer_device->device_id());
        CHECK(uf->get_id() == "some-id");
        CHECK(uf->get_max_sequence() == 15);
        CHECK(uf->get_index() == 22ul);
        CHECK(std::distance(cluster->get_pending_folders().begin(), cluster->get_pending_folders().end()) == 1);

        {
            // change unknown folder
            auto cc = std::make_unique<proto::ClusterConfig>();
            auto &pr_folder = proto::add_folders(*cc);
            proto::set_id(pr_folder, "some-id-n");
            proto::set_label(pr_folder, "some-label-N");
            auto &pr_device = proto::add_devices(pr_folder);
            proto::set_id(pr_device, peer_id.get_sha256());
            proto::set_name(pr_device, peer_device->get_name());
            proto::set_max_sequence(pr_device, 10);
            proto::set_index_id(pr_device, 22ul);

            diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
            REQUIRE(diff_opt);

            r_a = diff_opt.value()->apply(*controller, {});
            CHECK(r_a);

            CHECK(std::distance(cluster->get_pending_folders().begin(), cluster->get_pending_folders().end()) == 1);
        }
    }
    SECTION("existing folder") {
        db::Folder db_folder;
        db::set_id(db_folder, "some-id");
        db::set_label(db_folder, "some-label");
        db::set_path(db_folder, "/my/path");
        auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();
        folder->assign_cluster(cluster);

        cluster->get_folders().put(folder);

        auto folder_info_my = folder_info_ptr_t();
        auto folder_info_peer = folder_info_ptr_t();
        {
            db::FolderInfo db_fi;
            db::set_index_id(db_fi, 5ul);
            db::set_max_sequence(db_fi, 10l);
            folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
            folder->get_folder_infos().put(folder_info_my);
        }
        {
            db::FolderInfo db_fi;
            db::set_index_id(db_fi, 6ul);
            db::set_max_sequence(db_fi, 10l);
            folder_info_peer = folder_info_t::create(sequencer->next_uuid(), db_fi, peer_device, folder).value();
            folder->get_folder_infos().put(folder_info_peer);
        }

        auto cc = std::make_unique<proto::ClusterConfig>();
        auto &pr_folder = proto::add_folders(*cc);
        proto::set_id(pr_folder, folder->get_id());
        proto::set_label(pr_folder, folder->get_label());
        auto &pr_peer = proto::add_devices(pr_folder);
        proto::set_id(pr_peer, peer_id.get_sha256());
        proto::set_name(pr_peer, peer_device->get_name());
        proto::set_max_sequence(pr_peer, folder_info_peer->get_max_sequence());

        SECTION("nothing changed") {
            proto::set_index_id(pr_peer, folder_info_peer->get_index());

            auto diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
            REQUIRE(diff_opt);

            auto &diff = diff_opt.value();
            auto r_a = diff->apply(*controller, {});
            CHECK(r_a);
            auto pr_file = proto::FileInfo();
            proto::set_name(pr_file, "name.bin");
            proto::set_sequence(pr_file, folder_info_peer->get_max_sequence());
            auto &pr_version = proto::get_version(pr_file);
            proto::add_counters(pr_version, proto::Counter(peer_device->device_id().get_uint(), 0));
            auto peer_file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_info_peer).value();
            folder_info_peer->add_strict(peer_file);

            bool visited = false;
            auto visitor = my_cluster_update_visitor_t([&](auto &) { visited = true; });
            auto r_v = diff->visit(visitor, nullptr);
            REQUIRE(r_v);
            REQUIRE(visited);
            REQUIRE(visitor.updated_folders == 0);
        }
    }

    SECTION("reset index") {
        db::Folder db_folder;
        db::set_id(db_folder, "some-id");
        db::set_label(db_folder, "some-label");
        db::set_path(db_folder, "/my/path");
        auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();
        folder->assign_cluster(cluster);

        cluster->get_folders().put(folder);
        auto &folder_infos = folder->get_folder_infos();

        auto folder_info_my = folder_info_ptr_t();
        auto folder_info_peer = folder_info_ptr_t();
        {
            db::FolderInfo db_fi;
            db::set_index_id(db_fi, 5ul);
            db::set_max_sequence(db_fi, 10l);
            folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
            folder_infos.put(folder_info_my);
        }
        {
            db::FolderInfo db_fi;
            db::set_index_id(db_fi, 6ul);
            db::set_max_sequence(db_fi, 10l);
            folder_info_peer = folder_info_t::create(sequencer->next_uuid(), db_fi, peer_device, folder).value();
            folder_infos.put(folder_info_peer);

            auto pr_file = proto::FileInfo();
            proto::set_name(pr_file, "a.txt");
            proto::set_sequence(pr_file, 10l);
            auto &pr_version = proto::get_version(pr_file);
            proto::add_counters(pr_version, proto::Counter(peer_device->device_id().get_uint(), 0));
            auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_info_peer).value();
            folder_info_peer->get_file_infos().put(file);
        }

        auto cc = std::make_unique<proto::ClusterConfig>();
        auto &pr_folder = proto::add_folders(*cc);
        proto::set_id(pr_folder, folder->get_id());
        proto::set_label(pr_folder, folder->get_label());
        auto &pr_peer = proto::add_devices(pr_folder);
        proto::set_id(pr_peer, peer_id.get_sha256());
        proto::set_name(pr_peer, peer_device->get_name());

        SECTION("peer index has changed") {
            SECTION("non-zero index") {
                proto::set_index_id(pr_peer, 1234ul);
                proto::set_max_sequence(pr_peer, 123456u);

                auto diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
                REQUIRE(diff_opt);

                auto &diff = diff_opt.value();
                auto r_a = diff->apply(*controller, {});
                REQUIRE(r_a);
                auto fi = folder_infos.by_device(*peer_device);
                CHECK(fi->get_index() == 1234ul);
                CHECK(fi->get_max_sequence() == 0);
                CHECK(fi.get() == folder_info_peer.get());

                bool visited = false;
                auto visitor = my_cluster_update_visitor_t([&](auto &) { visited = true; });
                auto r_v = diff->visit(visitor, nullptr);
                REQUIRE(r_v);
                REQUIRE(visited);
                CHECK(visitor.remove_files == 1);
                CHECK(visitor.updated_folders == 1);
                CHECK(visitor.remove_folders == 0);
            }
            SECTION("zero index") {
                proto::set_index_id(pr_peer, 0ul);
                proto::set_max_sequence(pr_peer, 123456u);

                auto diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
                REQUIRE(diff_opt);

                auto &diff = diff_opt.value();
                auto r_a = diff->apply(*controller, {});
                REQUIRE(r_a);
                auto fi = folder_infos.by_device(*peer_device);
                CHECK(fi->get_index() == 0);
                CHECK(fi->get_max_sequence() == 0);
                CHECK(fi.get() == folder_info_peer.get());

                bool visited = false;
                auto visitor = my_cluster_update_visitor_t([&](auto &) { visited = true; });
                auto r_v = diff->visit(visitor, nullptr);
                REQUIRE(r_v);
                CHECK(visited);
                CHECK(visitor.remove_files == 1);
                CHECK(visitor.updated_folders == 1);
                CHECK(visitor.remove_folders == 0);
            }
        }
    }
}

TEST_CASE("cluster update, reset folder", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db::set_id(db_folder, "some-id");
    db::set_label(db_folder, "my-label");
    db::set_path(db_folder, "/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();
    folder->assign_cluster(cluster);

    cluster->get_folders().put(folder);
    auto db_p_folder = [&]() -> db::PendingFolder {
        db::PendingFolder db_p_folder;
        auto &f = db::get_folder(db_p_folder);
        db::set_id(f, "1111-2222");
        db::set_label(f, "unknown");
        return db_p_folder;
    }();
    auto u_folder = pending_folder_t::create(sequencer->next_uuid(), db_p_folder, peer_device->device_id()).value();
    auto &unknown_folders = cluster->get_pending_folders();
    unknown_folders.put(u_folder);

    auto folder_info_my = folder_info_ptr_t();
    auto folder_info_peer = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db::set_index_id(db_fi, 5ul);
        db::set_max_sequence(db_fi, 10l);
        folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
    }
    {
        db::FolderInfo db_fi;
        db::set_index_id(db_fi, 6ul);
        db::set_max_sequence(db_fi, 0l);
        folder_info_peer = folder_info_t::create(sequencer->next_uuid(), db_fi, peer_device, folder).value();
    }
    folder->get_folder_infos().put(folder_info_my);
    folder->get_folder_infos().put(folder_info_peer);

    auto bi1 = proto::BlockInfo();
    proto::set_size(bi1, 5);
    proto::set_hash(bi1, utils::sha256_digest(as_bytes("12345")).value());
    auto b1 = block_info_t::create(bi1).assume_value();

    auto bi2 = proto::BlockInfo();
    proto::set_size(bi2, 5);
    proto::set_hash(bi2, utils::sha256_digest(as_bytes("67890")).value());
    auto b2 = block_info_t::create(bi2).assume_value();

    auto bi3 = proto::BlockInfo();
    proto::set_size(bi3, 5);
    proto::set_hash(bi3, utils::sha256_digest(as_bytes("qqqqqq")).value());
    auto b3 = block_info_t::create(bi3).assume_value();

    auto &blocks_map = cluster->get_blocks();
    blocks_map.put(b1);
    blocks_map.put(b2);
    blocks_map.put(b3);

    proto::FileInfo pr_fi_my = [&]() {
        auto f = proto::FileInfo();
        proto::set_name(f, "a/b.txt");
        proto::set_size(f, 5ul);
        proto::set_block_size(f, 5ul);
        proto::set_sequence(f, 1);
        proto::add_blocks(f, bi1);
        auto &v = proto::get_version(f);
        proto::add_counters(v, proto::Counter(my_device->device_id().get_uint(), 0));
        return f;
    }();
    auto fi_my = file_info_t::create(sequencer->next_uuid(), pr_fi_my, folder_info_my).value();
    folder_info_my->add_strict(fi_my);

    proto::FileInfo pr_fi_peer1 = [&]() {
        auto f = proto::FileInfo();
        proto::set_name(f, "a/c.txt");
        proto::set_size(f, 5ul);
        proto::set_block_size(f, 5ul);
        proto::set_sequence(f, 1);
        proto::add_blocks(f, bi2);
        auto &v = proto::get_version(f);
        proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 0));
        return f;
    }();
    auto fi_peer1 = file_info_t::create(sequencer->next_uuid(), pr_fi_peer1, folder_info_peer).value();
    REQUIRE(folder_info_peer->add_strict(fi_peer1));
    REQUIRE(folder_info_peer->get_file_infos().size() == 1);

    proto::FileInfo pr_fi_peer2 = [&]() {
        auto f = proto::FileInfo();
        proto::set_name(f, "a/d.txt");
        proto::set_size(f, 10ul);
        proto::set_block_size(f, 5ul);
        proto::set_sequence(f, 2);
        proto::add_blocks(f, bi2);
        proto::add_blocks(f, bi3);
        auto &v = proto::get_version(f);
        proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 0));
        return f;
    }();
    auto fi_peer2 = file_info_t::create(sequencer->next_uuid(), pr_fi_peer2, folder_info_peer).value();
    REQUIRE(folder_info_peer->add_strict(fi_peer2));
    REQUIRE(folder_info_peer->get_file_infos().size() == 2);

    fi_my->assign_block(b1.get(), 0);
    fi_peer1->assign_block(b2.get(), 0);
    fi_peer2->assign_block(b2.get(), 0);
    fi_peer2->assign_block(b3.get(), 1);

    auto cc = std::make_unique<proto::ClusterConfig>();
    auto &pr_folder = proto::add_folders(*cc);
    proto::set_id(pr_folder, folder->get_id());
    proto::set_label(pr_folder, folder->get_label());
    auto &pr_peer = proto::add_devices(pr_folder);
    proto::set_id(pr_peer, peer_id.get_sha256());
    proto::set_name(pr_peer, peer_device->get_name());
    proto::set_max_sequence(pr_peer, 123456u);
    proto::set_index_id(pr_peer, 7u);

    auto diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
    REQUIRE(diff_opt);

    auto &diff = diff_opt.value();
    REQUIRE(diff->apply(*controller, {}));

    auto folder_info_peer_new = folder->get_folder_infos().by_device(*peer_device);
    REQUIRE(folder_info_peer_new);
    REQUIRE(folder_info_peer_new == folder_info_peer);
    REQUIRE(folder_info_peer_new->get_file_infos().size() == 0);
    CHECK(folder_info_peer_new->get_index() == 7ul);
    CHECK(folder_info_peer_new->get_max_sequence() == 0);

    CHECK(folder_info_peer->get_file_infos().size() == 0);
    CHECK(fi_peer1->use_count() == 1);
    CHECK(fi_peer2->use_count() == 1);

    CHECK(blocks_map.size() == 1);
    CHECK(blocks_map.by_hash(b1->get_hash()));

    bool visited = false;
    auto visitor = my_cluster_update_visitor_t([&](auto &) { visited = true; });
    auto r_v = diff->visit(visitor, nullptr);
    REQUIRE(r_v);
    REQUIRE(visited);
    CHECK(visitor.remove_blocks == 1);
    CHECK(visitor.remove_files == 1);
    CHECK(visitor.remove_folders == 0);
    CHECK(visitor.remove_pending_folders == 1);
    CHECK(visitor.updated_folders == 1);
}

TEST_CASE("cluster update for a folder, which was not shared", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db::set_id(db_folder, "some-id");
    db::set_label(db_folder, "my-label");
    db::set_path(db_folder, "/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();
    folder->assign_cluster(cluster);

    cluster->get_folders().put(folder);

    auto folder_info_my = folder_info_ptr_t();
    auto folder_info_peer = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db::set_index_id(db_fi, 5ul);
        db::set_max_sequence(db_fi, 10l);
        folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }

    auto cc = std::make_unique<proto::ClusterConfig>();
    auto &pr_folder = proto::add_folders(*cc);
    proto::set_id(pr_folder, folder->get_id());
    proto::set_label(pr_folder, folder->get_label());
    auto &pr_peer = proto::add_devices(pr_folder);
    proto::set_id(pr_peer, peer_id.get_sha256());
    proto::set_name(pr_peer, peer_device->get_name());
    proto::set_max_sequence(pr_peer, 123456u);
    proto::set_index_id(pr_peer, 7u);

    auto diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
    REQUIRE(diff_opt);
    auto &diff = diff_opt.value();
    REQUIRE(diff->apply(*controller, {}));
}

TEST_CASE("cluster update with unknown devices", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id_1 =
        device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_id_2 =
        device_id_t::from_string("EAMTZPW-Q4QYERN-D57DHFS-AUP2OMG-PAHOR3R-ZWLKGAA-WQC5SVW-UJ5NXQA").value();

    auto peer_device = device_t::create(peer_id_1, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db::set_id(db_folder, "some-id");
    db::set_label(db_folder, "my-label");
    db::set_path(db_folder, "/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();

    cluster->get_folders().put(folder);

    auto folder_info_my = folder_info_ptr_t();
    auto folder_info_peer = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db::set_index_id(db_fi, 5ul);
        db::set_max_sequence(db_fi, 10l);
        folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }
    {
        db::FolderInfo db_fi;
        db::set_index_id(db_fi, 6ul);
        db::set_max_sequence(db_fi, 0l);
        folder_info_peer = folder_info_t::create(sequencer->next_uuid(), db_fi, peer_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }
    folder->get_folder_infos().put(folder_info_my);
    folder->get_folder_infos().put(folder_info_peer);

    auto cc = std::make_unique<proto::ClusterConfig>();

    auto &pr_folder = proto::add_folders(*cc);
    proto::set_id(pr_folder, folder->get_id());
    proto::set_label(pr_folder, folder->get_label());

    auto &pr_peer_1 = proto::add_devices(pr_folder);
    proto::set_id(pr_peer_1, peer_id_1.get_sha256());
    proto::set_name(pr_peer_1, peer_device->get_name());
    proto::set_max_sequence(pr_peer_1, 123456u);
    proto::set_index_id(pr_peer_1, 7u);

    auto &pr_peer_2 = proto::add_devices(pr_folder);
    proto::set_id(pr_peer_2, peer_id_2.get_sha256());
    proto::set_name(pr_peer_2, peer_device->get_name());
    proto::set_max_sequence(pr_peer_2, 123456u);
    proto::set_index_id(pr_peer_2, 7u);

    auto diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
    REQUIRE(diff_opt);
}

TEST_CASE("cluster update nothing shared", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);
    auto &blocks_map = cluster->get_blocks();

    auto bi1 = proto::BlockInfo();
    proto::set_size(bi1, 5);
    proto::set_hash(bi1, utils::sha256_digest(as_bytes("12345")).value());
    auto b1 = block_info_t::create(bi1).assume_value();
    blocks_map.put(b1);

    auto sha256 = peer_id.get_sha256();
    auto folder_1_id = std::string_view("1234-5678");
    auto folder_2_id = std::string_view("5678-7233");

    proto::FileInfo pr_fi_peer = [&]() {
        auto f = proto::FileInfo();
        proto::set_name(f, "a/c.txt");
        proto::set_size(f, 5ul);
        proto::set_block_size(f, 5ul);
        proto::set_sequence(f, 5);
        proto::add_blocks(f, bi1);
        auto &v = proto::get_version(f);
        proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 0));
        return f;
    }();

    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder(folder_1_id, "/p1").upsert_folder(folder_2_id, "/p1").apply());
    REQUIRE(builder.share_folder(sha256, folder_1_id).share_folder(sha256, folder_2_id).apply());
    REQUIRE(builder.make_index(sha256, folder_1_id).add(pr_fi_peer, peer_device, false).finish().apply());
    REQUIRE(builder.make_index(sha256, folder_2_id).add(pr_fi_peer, peer_device, false).finish().apply());

    REQUIRE(blocks_map.size() == 1);
    auto folder_1 = cluster->get_folders().by_id(folder_1_id);
    auto folder_2 = cluster->get_folders().by_id(folder_2_id);
    auto folder_1_peer = folder_1->get_folder_infos().by_device(*peer_device);
    auto folder_2_peer = folder_2->get_folder_infos().by_device(*peer_device);
    REQUIRE(folder_1_peer->get_file_infos().size() == 1);
    REQUIRE(folder_2_peer->get_file_infos().size() == 1);

    auto cc = std::make_unique<proto::ClusterConfig>();
    auto diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
    REQUIRE(diff_opt);
    auto opt = diff_opt.value()->apply(*controller, {});
    REQUIRE(opt);

    CHECK(blocks_map.size() == 0);
    CHECK(folder_1_peer->get_file_infos().size() == 0);
    CHECK(folder_1->is_shared_with(*peer_device));
    CHECK(folder_1->is_shared_with(*peer_device)->get_file_infos().size() == 0);
    CHECK(folder_2_peer->get_file_infos().size() == 0);
    CHECK(folder_2->is_shared_with(*peer_device));
    CHECK(folder_2->is_shared_with(*peer_device)->get_file_infos().size() == 0);
}

TEST_CASE("non-shared pending folder", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id_1 =
        device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id_1, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto builder = diff_builder_t(*cluster);
    auto folder_id = "1234";

    auto sha256 = peer_id_1.get_sha256();
    REQUIRE(builder.upsert_folder(folder_id, "/my/path").apply());
    REQUIRE(builder.configure_cluster(sha256).add(sha256, folder_id, 5, 4).finish().apply());
    REQUIRE(cluster->get_pending_folders().size() == 1);

    auto &uf = *cluster->get_pending_folders().begin()->item;
    REQUIRE(uf.device_id() == peer_id_1);
    REQUIRE(uf.get_index() == 5);
    REQUIRE(uf.get_max_sequence() == 4);
}

TEST_CASE("cluster update with remote folders (1)", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id_1 =
        device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id_1, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db::set_id(db_folder, "some-id");
    db::set_label(db_folder, "my-label");
    db::set_path(db_folder, "/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();

    cluster->get_folders().put(folder);

    auto folder_info_my = folder_info_ptr_t();
    auto folder_info_peer = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db::set_index_id(db_fi, 5ul);
        db::set_max_sequence(db_fi, 10l);
        folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }
    {
        db::FolderInfo db_fi;
        db::set_index_id(db_fi, 6ul);
        db::set_max_sequence(db_fi, 0l);
        folder_info_peer = folder_info_t::create(sequencer->next_uuid(), db_fi, peer_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }
    folder->get_folder_infos().put(folder_info_my);
    folder->get_folder_infos().put(folder_info_peer);

    auto cc = std::make_unique<proto::ClusterConfig>();

    auto &pr_folder = proto::add_folders(*cc);
    proto::set_id(pr_folder, folder->get_id());
    proto::set_label(pr_folder, folder->get_label());

    auto &pr_peer = proto::add_devices(pr_folder);
    proto::set_id(pr_peer, peer_id_1.get_sha256());
    proto::set_name(pr_peer, peer_device->get_name());
    proto::set_max_sequence(pr_peer, 123456u);
    proto::set_index_id(pr_peer, 7u);

    auto &pr_peer_my = proto::add_devices(pr_folder);
    proto::set_id(pr_peer_my, my_id.get_sha256());
    proto::set_name(pr_peer_my, my_device->get_name());
    proto::set_max_sequence(pr_peer_my, 3);
    proto::set_index_id(pr_peer_my, 5ul);

    auto diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
    REQUIRE(diff_opt);

    auto opt = diff_opt.value()->apply(*controller, {});
    REQUIRE(opt);

    auto remote_folder = peer_device->get_remote_folder_infos().by_folder(*folder);
    REQUIRE(remote_folder);
    CHECK(remote_folder->get_index() == 5ul);
    CHECK(remote_folder->get_max_sequence() == 3);

    SECTION("unshare by peer") {
        auto cc = std::make_unique<proto::ClusterConfig>();
        diff_opt = diff::peer::cluster_update_t::create({}, *cluster, *sequencer, *peer_device, *cc);
        REQUIRE(diff_opt);

        auto &diff = diff_opt.value();
        REQUIRE(diff->apply(*controller, {}));

        CHECK(peer_device->get_remote_folder_infos().size() == 0);
        auto fi = folder->get_folder_infos().by_device(*peer_device);
        // we are still sharing the folder with peer
        CHECK(fi);
    }
}

TEST_CASE("cluster update, inactual remote folders for non-shared folder", "[model]") {
    auto root_path = unique_path();
    auto path_guard = path_guard_t(root_path);

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    auto &devices = cluster->get_devices();
    devices.put(my_device);
    devices.put(peer_device);

    auto builder = diff_builder_t(*cluster);
    auto folder_1_id = "1234";

    auto sha256 = peer_id.get_sha256();

    auto r = builder.configure_cluster(sha256, root_path)
                 .add(sha256, folder_1_id, 5, 4)
                 .add(my_id.get_sha256(), folder_1_id, 55, 44)
                 .finish()
                 .apply();
    REQUIRE(r);
    REQUIRE(cluster->get_folders().size() == 0);
}

TEST_CASE("device introduction", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id_1 =
        device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_id_2 =
        device_id_t::from_string("EAMTZPW-Q4QYERN-D57DHFS-AUP2OMG-PAHOR3R-ZWLKGAA-WQC5SVW-UJ5NXQA").value();

    auto peer_device_1 = device_t::create(peer_id_1, "peer-device").value();
    auto db_peer_1 = db::Device();
    peer_device_1->serialize(db_peer_1);
    db::set_introducer(db_peer_1, true);
    REQUIRE(peer_device_1->update(db_peer_1));

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    auto &devices = cluster->get_devices();
    devices.put(my_device);
    devices.put(peer_device_1);

    auto builder = diff_builder_t(*cluster);
    auto folder_1_id = "1234";
    auto folder_2_id = "5678";
    auto folder_3_id = "abcd";

    auto sha256_1 = peer_id_1.get_sha256();
    auto sha256_2 = peer_id_2.get_sha256();

    auto r = builder.upsert_folder(folder_1_id, "/my/path-1")
                 .upsert_folder(folder_2_id, "/my/path-2")
                 .upsert_folder(folder_3_id, "/my/path-3")
                 .then()
                 .share_folder(sha256_1, folder_1_id, {})
                 .share_folder(sha256_1, folder_2_id, {})
                 .share_folder(sha256_1, folder_3_id, {})
                 .apply();
    REQUIRE(r);

    SECTION("non-emtpy introduced device address") {
        SECTION("dynamic address") {
            r = builder.configure_cluster(sha256_1)
                    .add(sha256_1, folder_1_id, 5, 4)
                    .add(sha256_2, folder_1_id, 55, 444, "dynamic")
                    .finish()
                    .apply();
            REQUIRE(r);
        }
        SECTION("invalid address") {
            auto ec = builder.configure_cluster(sha256_1)
                          .add(sha256_1, folder_1_id, 5, 4)
                          .add(sha256_2, folder_1_id, 55, 444, "some-invalid-url%%!")
                          .fail();
            auto expected_ec = utils::make_error_code(utils::error_code_t::malformed_url);
            REQUIRE(ec.value() == expected_ec.value());
            REQUIRE(ec.message() == expected_ec.message());
        }
    }

    SECTION("emtpy introduced device address") {
        r = builder.configure_cluster(sha256_1)
                .add(sha256_1, folder_1_id, 5, 4)
                .add(sha256_2, folder_1_id, 55, 444)
                .add(sha256_1, folder_2_id, 6, 4)
                .add(sha256_2, folder_2_id, 66, 444)
                .finish()
                .apply();
        REQUIRE(r);

        REQUIRE(devices.size() == 3);

        auto peer_device_2 = devices.by_sha256(peer_id_2.get_sha256());
        REQUIRE(peer_device_2);
        auto folder_1 = cluster->get_folders().by_id(folder_1_id);
        auto folder_2 = cluster->get_folders().by_id(folder_2_id);
        auto folder_3 = cluster->get_folders().by_id(folder_3_id);
        REQUIRE(folder_1->is_shared_with(*peer_device_2));
        REQUIRE(folder_2->is_shared_with(*peer_device_2));
        REQUIRE(!folder_3->is_shared_with(*peer_device_2));

        SECTION("introduce 3rd folder") {
            r = builder.configure_cluster(sha256_1)
                    .add(sha256_1, folder_1_id, 5, 4)
                    .add(sha256_2, folder_1_id, 55, 444)
                    .add(sha256_1, folder_2_id, 6, 4)
                    .add(sha256_2, folder_2_id, 66, 444)
                    .add(sha256_1, folder_3_id, 7, 4)
                    .add(sha256_2, folder_3_id, 77, 444)
                    .finish()
                    .apply();
            REQUIRE(r);
            CHECK(devices.size() == 3);
            CHECK(folder_1->is_shared_with(*peer_device_2));
            CHECK(folder_2->is_shared_with(*peer_device_2));
            CHECK(folder_3->is_shared_with(*peer_device_2));
        }
        SECTION("de-introduce 1 folder") {
            r = builder.configure_cluster(sha256_1)
                    .add(sha256_1, folder_1_id, 5, 4)
                    .add(sha256_2, folder_1_id, 55, 444)
                    .add(sha256_1, folder_2_id, 6, 4)
                    .finish()
                    .apply();
            REQUIRE(r);
            CHECK(devices.size() == 3);
            CHECK(folder_1->is_shared_with(*peer_device_2));
            CHECK(!folder_2->is_shared_with(*peer_device_2));
        }
        SECTION("de-introduce all folders => de-introduce device") {
            r = builder.configure_cluster(sha256_1)
                    .add(sha256_1, folder_1_id, 5, 4)
                    .add(sha256_1, folder_2_id, 6, 4)
                    .finish()
                    .apply();
            REQUIRE(r);
            CHECK(devices.size() == 2);
            CHECK(!folder_1->is_shared_with(*peer_device_2));
            CHECK(!folder_2->is_shared_with(*peer_device_2));
        }
        SECTION("don't de-introduce folders/devices if skip_introduction_removals") {
            db::set_skip_introduction_removals(db_peer_1, true);
            REQUIRE(peer_device_1->update(db_peer_1));

            r = builder.configure_cluster(sha256_1)
                    .add(sha256_1, folder_1_id, 5, 4)
                    .add(sha256_1, folder_2_id, 6, 4)
                    .finish()
                    .apply();
            REQUIRE(r);
            CHECK(devices.size() == 3);
            CHECK(folder_1->is_shared_with(*peer_device_2));
            CHECK(folder_2->is_shared_with(*peer_device_2));
        }
    }
}

TEST_CASE("auto-accept folders", "[model]") {
    auto root_path = unique_path();
    auto path_guard = path_guard_t(root_path);

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id_1 =
        device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_id_2 =
        device_id_t::from_string("EAMTZPW-Q4QYERN-D57DHFS-AUP2OMG-PAHOR3R-ZWLKGAA-WQC5SVW-UJ5NXQA").value();

    auto peer_device_1 = device_t::create(peer_id_1, "peer-device").value();
    auto db_peer_1 = db::Device();
    peer_device_1->serialize(db_peer_1);
    db::set_introducer(db_peer_1, true);
    db::set_auto_accept(db_peer_1, true);
    REQUIRE(peer_device_1->update(db_peer_1));

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    auto &devices = cluster->get_devices();
    devices.put(my_device);
    devices.put(peer_device_1);

    auto peer_device_2 = device_t::create(peer_id_2, "peer-device").value();

    auto builder = diff_builder_t(*cluster);
    auto folder_1_id = "1234";

    auto sha256_1 = peer_id_1.get_sha256();
    auto sha256_2 = peer_id_2.get_sha256();

    SECTION("folder already exists => just share it") {
        auto r = builder.upsert_folder(folder_1_id, "/my/path-1").apply();
        REQUIRE(r);

        r = builder.configure_cluster(sha256_1, root_path).add(sha256_1, folder_1_id, 5, 4).finish().apply();
        REQUIRE(r);

        REQUIRE(cluster->get_folders().size() == 1);
        auto folder_1 = cluster->get_folders().by_id(folder_1_id);
        REQUIRE(folder_1->is_shared_with(*peer_device_1));
    }
    SECTION("able to create dir by folder_id") {
        auto r = builder.configure_cluster(sha256_1, root_path).add(sha256_1, folder_1_id, 5, 4).finish().apply();
        REQUIRE(r);

        REQUIRE(cluster->get_folders().size() == 1);
        auto folder_1 = cluster->get_folders().by_id(folder_1_id);
        REQUIRE(folder_1->is_shared_with(*peer_device_1));
        CHECK(bfs::exists(root_path / folder_1_id));
    }
    SECTION("able to create dir by folder label") {
        auto r = builder.configure_cluster(sha256_1, root_path)
                     .add_named(sha256_1, folder_1_id, 5, 4, "zzz")
                     .finish()
                     .apply();
        REQUIRE(r);

        REQUIRE(cluster->get_folders().size() == 1);
        auto folder_1 = cluster->get_folders().by_id(folder_1_id);
        REQUIRE(folder_1->is_shared_with(*peer_device_1));
        CHECK(bfs::exists(root_path / "zzz"));
    }
#ifndef SYNCSPIRIT_WIN
    SECTION("not able to create dir by folder_id") {
        auto new_root = root_path / "sub-root";
        bfs::create_directories(new_root);
        bfs::permissions(new_root, bfs::perms::all, bfs::perm_options::remove);
        auto new_guard = path_guard_t(new_root);

        auto r = builder.configure_cluster(sha256_1, new_root).add(sha256_1, folder_1_id, 5, 4).fail();
        REQUIRE(r);

        REQUIRE(cluster->get_folders().size() == 0);
        REQUIRE(!cluster->get_folders().by_id(folder_1_id));

        bfs::permissions(new_root, bfs::perms::all, bfs::perm_options::add);
        CHECK(!bfs::exists(new_root / folder_1_id));
    }
#endif
    SECTION("auto accept + introduce peer (source first)") {
        auto r = builder.configure_cluster(sha256_1, root_path)
                     .add(sha256_1, folder_1_id, 5, 4)
                     .add(sha256_2, folder_1_id, 55, 44)
                     .finish()
                     .apply();
        REQUIRE(r);

        REQUIRE(cluster->get_folders().size() == 1);
        REQUIRE(cluster->get_devices().size() == 3);
        auto folder_1 = cluster->get_folders().by_id(folder_1_id);
        REQUIRE(folder_1->is_shared_with(*peer_device_1));
        REQUIRE(folder_1->is_shared_with(*peer_device_2));
        CHECK(bfs::exists(root_path / folder_1_id));
    }
    SECTION("auto accept + introduce peer (source second)") {
        auto r = builder.configure_cluster(sha256_1, root_path)
                     .add(sha256_2, folder_1_id, 55, 44)
                     .add(sha256_1, folder_1_id, 5, 4)
                     .finish()
                     .apply();
        REQUIRE(r);

        REQUIRE(cluster->get_folders().size() == 1);
        REQUIRE(cluster->get_devices().size() == 3);
        auto folder_1 = cluster->get_folders().by_id(folder_1_id);
        REQUIRE(folder_1->is_shared_with(*peer_device_1));
        REQUIRE(folder_1->is_shared_with(*peer_device_2));
        CHECK(bfs::exists(root_path / folder_1_id));
    }
#ifndef SYNCSPIRIT_WIN
    SECTION("not able to create dir (source second)") {
        auto new_root = root_path / "sub-root";
        bfs::create_directories(new_root);
        bfs::permissions(new_root, bfs::perms::all, bfs::perm_options::remove);
        auto new_guard = path_guard_t(new_root);

        auto r = builder.configure_cluster(sha256_1, new_root)
                     .add(sha256_2, folder_1_id, 55, 44)
                     .add(sha256_1, folder_1_id, 5, 4)
                     .fail();
        REQUIRE(r);

        REQUIRE(cluster->get_folders().size() == 0);
        REQUIRE(cluster->get_devices().size() == 2);
        REQUIRE(!cluster->get_folders().by_id(folder_1_id));

        bfs::permissions(new_root, bfs::perms::all, bfs::perm_options::add);
        CHECK(!bfs::exists(new_root / folder_1_id));
    }
#endif
}

TEST_CASE("redundant shares", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto db_peer = db::Device();
    peer_device->serialize(db_peer);
    db::set_introducer(db_peer, true);
    REQUIRE(peer_device->update(db_peer));

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    auto &devices = cluster->get_devices();
    devices.put(my_device);
    devices.put(peer_device);

    auto builder = diff_builder_t(*cluster);
    auto folder_id = "1234";

    auto sha256 = peer_id.get_sha256();

    auto r = builder.upsert_folder(folder_id, "/my/path-1").then().share_folder(sha256, folder_id, {}).apply();
    REQUIRE(r);

    r = builder.configure_cluster(sha256).add(sha256, folder_id, 5, 4).add(sha256, folder_id, 6, 7).finish().apply();
    REQUIRE(r);

    auto folder = cluster->get_folders().by_id(folder_id);
    auto folder_peer = folder->get_folder_infos().by_device(*peer_device);
    CHECK(folder_peer->get_index() == 5);
}

TEST_CASE("folder is shared peer (seq=0), but peer does not accepts the share => NOOP", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto db_peer = db::Device();
    peer_device->serialize(db_peer);
    db::set_introducer(db_peer, true);
    REQUIRE(peer_device->update(db_peer));

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    auto &devices = cluster->get_devices();
    devices.put(my_device);
    devices.put(peer_device);

    auto builder = diff_builder_t(*cluster);
    auto folder_id = "1234";

    auto sha256 = peer_id.get_sha256();

    auto r = builder.upsert_folder(folder_id, "/my/path-1").then().share_folder(sha256, folder_id, {}).apply();
    REQUIRE(r);

    auto folder = cluster->get_folders().by_id(folder_id);
    auto folder_peer = folder->get_folder_infos().by_device(*peer_device);

    CHECK(folder_peer->get_index() == 0);
    CHECK(folder_peer->get_max_sequence() == 0);

    r = builder.configure_cluster(sha256).finish().apply();
    REQUIRE(r);

    CHECK(folder_peer.get() == folder->get_folder_infos().by_device(*peer_device).get());
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
