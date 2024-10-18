// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

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
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    SECTION("unknown folder") {
        auto cc = std::make_unique<proto::ClusterConfig>();
        auto folder_ptr = cc->add_folders();
        folder_ptr->set_id("some-id");
        folder_ptr->set_label("some-label");
        auto d_peer = folder_ptr->add_devices();
        d_peer->set_id(std::string(peer_id.get_sha256()));
        d_peer->set_name(std::string(peer_device->get_name()));
        d_peer->set_max_sequence(10);
        d_peer->set_index_id(22ul);

        auto folder = *folder_ptr;
        auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
        REQUIRE(diff_opt);

        auto &diff = diff_opt.value();
        auto r_a = diff->apply(*cluster);
        CHECK(r_a);

        auto visitor = my_cluster_update_visitor_t([&](auto &diff) {});
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
        auto mf = db_pf.mutable_folder();
        mf->set_id(folder.id());
        auto mfi = db_pf.mutable_folder_info();
        mfi->set_max_sequence(d_peer->max_sequence());
        mfi->set_index_id(d_peer->index_id());

        diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
        REQUIRE(diff_opt);
        diff = diff_opt.value();
        r_a = diff->apply(*cluster);
        CHECK(r_a);
        REQUIRE(cluster->get_pending_folders().size() == 1);
        std::ignore = diff->visit(visitor, nullptr);
        REQUIRE(visitor.add_pending_folders == 1);

        // max-id changed
        d_peer->set_max_sequence(15);
        diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
        REQUIRE(diff_opt);
        diff = diff_opt.value();
        r_a = diff->apply(*cluster);
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

        // change unknown folder
        cc = std::make_unique<proto::ClusterConfig>();
        folder_ptr = cc->add_folders();
        folder_ptr->set_id("some-id-n");
        folder_ptr->set_label("some-label-N");
        d_peer = folder_ptr->add_devices();
        d_peer->set_id(std::string(peer_id.get_sha256()));
        d_peer->set_name(std::string(peer_device->get_name()));
        d_peer->set_max_sequence(10);
        d_peer->set_index_id(22ul);

        diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
        REQUIRE(diff_opt);

        r_a = diff_opt.value()->apply(*cluster);
        CHECK(r_a);

        CHECK(std::distance(cluster->get_pending_folders().begin(), cluster->get_pending_folders().end()) == 1);
    }

    SECTION("existing folder") {
        db::Folder db_folder;
        db_folder.set_id("1234-5678");
        db_folder.set_label("my-label");
        db_folder.set_path("/my/path");
        auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();

        cluster->get_folders().put(folder);

        auto folder_info_my = folder_info_ptr_t();
        auto folder_info_peer = folder_info_ptr_t();
        {
            db::FolderInfo db_fi;
            db_fi.set_index_id(5ul);
            db_fi.set_max_sequence(10l);
            folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
            folder->get_folder_infos().put(folder_info_my);
        }
        {
            db::FolderInfo db_fi;
            db_fi.set_index_id(6ul);
            db_fi.set_max_sequence(10l);
            folder_info_peer = folder_info_t::create(sequencer->next_uuid(), db_fi, peer_device, folder).value();
            folder->get_folder_infos().put(folder_info_peer);
        }

        auto cc = std::make_unique<proto::ClusterConfig>();
        auto p_folder = cc->add_folders();
        p_folder->set_id(std::string(folder->get_id()));
        p_folder->set_label(std::string(folder->get_label()));
        auto p_peer = p_folder->add_devices();
        p_peer->set_max_sequence(folder_info_peer->get_max_sequence());
        p_peer->set_id(std::string(peer_id.get_sha256()));
        p_peer->set_name(std::string(peer_device->get_name()));

        SECTION("nothing changed") {
            p_peer->set_max_sequence(folder_info_peer->get_max_sequence());
            p_peer->set_index_id(folder_info_peer->get_index());

            auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
            REQUIRE(diff_opt);

            auto &diff = diff_opt.value();
            auto r_a = diff->apply(*cluster);
            CHECK(r_a);
            auto pr_file = proto::FileInfo();
            pr_file.set_sequence(folder_info_peer->get_max_sequence());
            auto peer_file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_info_peer).value();
            folder_info_peer->add(peer_file, false);

            bool visited = false;
            auto visitor = my_cluster_update_visitor_t([&](auto &diff) { visited = true; });
            auto r_v = diff->visit(visitor, nullptr);
            REQUIRE(r_v);
            REQUIRE(visited);
            REQUIRE(visitor.updated_folders == 0);
        }

        SECTION("max sequence increased") {
            auto max_seq = folder_info_peer->get_max_sequence() + 1;
            p_peer->set_max_sequence(max_seq);
            p_peer->set_index_id(folder_info_peer->get_index());

            auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
            REQUIRE(diff_opt);

            auto &diff = diff_opt.value();
            auto r_a = diff->apply(*cluster);
            CHECK(r_a);

            bool visited = false;
            auto visitor = my_cluster_update_visitor_t([&](auto &diff) { visited = true; });
            auto r_v = diff->visit(visitor, nullptr);
            REQUIRE(r_v);
            REQUIRE(visited);
            REQUIRE(visitor.updated_folders == 1);
            REQUIRE(folder_info_peer->get_max_sequence() == max_seq);
        }
    }

    SECTION("reset index") {
        db::Folder db_folder;
        db_folder.set_id("1234-5678");
        db_folder.set_label("my-label");
        db_folder.set_path("/my/path");
        auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();

        cluster->get_folders().put(folder);
        auto &folder_infos = folder->get_folder_infos();

        auto folder_info_my = folder_info_ptr_t();
        auto folder_info_peer = folder_info_ptr_t();
        {
            db::FolderInfo db_fi;
            db_fi.set_index_id(5ul);
            db_fi.set_max_sequence(10l);
            folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
            folder_infos.put(folder_info_my);
        }
        {
            db::FolderInfo db_fi;
            db_fi.set_index_id(6ul);
            db_fi.set_max_sequence(10l);
            folder_info_peer = folder_info_t::create(sequencer->next_uuid(), db_fi, peer_device, folder).value();
            folder_infos.put(folder_info_peer);
        }

        auto cc = std::make_unique<proto::ClusterConfig>();
        auto p_folder = cc->add_folders();
        p_folder->set_id(std::string(folder->get_id()));
        p_folder->set_label(std::string(folder->get_label()));
        auto p_peer = p_folder->add_devices();
        p_peer->set_id(std::string(peer_id.get_sha256()));
        p_peer->set_name(std::string(peer_device->get_name()));

        SECTION("peer index has changed") {
            p_peer->set_max_sequence(123456u);
            p_peer->set_index_id(7ul);

            auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
            REQUIRE(diff_opt);

            auto &diff = diff_opt.value();
            auto r_a = diff->apply(*cluster);
            REQUIRE(r_a);
            auto fi = folder_infos.by_device(*peer_device);
            REQUIRE(fi->get_index() == 7ul);
            REQUIRE(fi->get_max_sequence() == 123456u);
            REQUIRE(fi.get() == folder_info_peer.get());

            bool visited = false;
            auto visitor = my_cluster_update_visitor_t([&](auto &diff) { visited = true; });
            auto r_v = diff->visit(visitor, nullptr);
            REQUIRE(r_v);
            REQUIRE(visited);
            REQUIRE(visitor.updated_folders == 1);
        }
    }
}

TEST_CASE("cluster update, reset folder", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();

    cluster->get_folders().put(folder);
    db::PendingFolder db_p_folder;
    db_p_folder.mutable_folder()->set_id("1111-2222");
    db_p_folder.mutable_folder()->set_label("unknown");
    auto u_folder = pending_folder_t::create(sequencer->next_uuid(), db_p_folder, peer_device->device_id()).value();
    auto &unknown_folders = cluster->get_pending_folders();
    unknown_folders.put(u_folder);

    auto folder_info_my = folder_info_ptr_t();
    auto folder_info_peer = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(5ul);
        db_fi.set_max_sequence(10l);
        folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
    }
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(6ul);
        db_fi.set_max_sequence(0l);
        folder_info_peer = folder_info_t::create(sequencer->next_uuid(), db_fi, peer_device, folder).value();
    }
    folder->get_folder_infos().put(folder_info_my);
    folder->get_folder_infos().put(folder_info_peer);

    auto bi1 = proto::BlockInfo();
    bi1.set_size(5);
    bi1.set_hash(utils::sha256_digest("12345").value());
    auto b1 = block_info_t::create(bi1).assume_value();

    auto bi2 = proto::BlockInfo();
    bi2.set_size(5);
    bi2.set_hash(utils::sha256_digest("67890").value());
    auto b2 = block_info_t::create(bi2).assume_value();

    auto bi3 = proto::BlockInfo();
    bi3.set_size(5);
    bi3.set_hash(utils::sha256_digest("qqqqqq").value());
    auto b3 = block_info_t::create(bi3).assume_value();

    auto &blocks_map = cluster->get_blocks();
    blocks_map.put(b1);
    blocks_map.put(b2);
    blocks_map.put(b3);

    proto::FileInfo pr_fi_my;
    pr_fi_my.set_name("a/b.txt");
    pr_fi_my.set_size(5ul);
    pr_fi_my.set_block_size(5ul);
    auto fi_my = file_info_t::create(sequencer->next_uuid(), pr_fi_my, folder_info_my).value();
    folder_info_my->add(fi_my, false);

    proto::FileInfo pr_fi_peer1;
    pr_fi_peer1.set_name("a/c.txt");
    pr_fi_peer1.set_size(5ul);
    pr_fi_peer1.set_block_size(5ul);
    auto fi_peer1 = file_info_t::create(sequencer->next_uuid(), pr_fi_peer1, folder_info_peer).value();
    folder_info_peer->add(fi_peer1, false);
    REQUIRE(folder_info_peer->get_file_infos().size() == 1);

    proto::FileInfo pr_fi_peer2;
    pr_fi_peer2.set_name("a/d.txt");
    pr_fi_peer2.set_size(10ul);
    pr_fi_peer2.set_block_size(5ul);
    auto fi_peer2 = file_info_t::create(sequencer->next_uuid(), pr_fi_peer2, folder_info_peer).value();
    folder_info_peer->add(fi_peer2, false);
    REQUIRE(folder_info_peer->get_file_infos().size() == 2);

    fi_my->assign_block(b1, 0);
    fi_peer1->assign_block(b2, 0);
    fi_peer2->assign_block(b2, 0);
    fi_peer2->assign_block(b3, 1);

    auto cc = std::make_unique<proto::ClusterConfig>();
    auto p_folder = cc->add_folders();
    p_folder->set_id(std::string(folder->get_id()));
    p_folder->set_label(std::string(folder->get_label()));
    auto p_peer = p_folder->add_devices();
    p_peer->set_id(std::string(peer_id.get_sha256()));
    p_peer->set_name(std::string(peer_device->get_name()));
    p_peer->set_max_sequence(123456u);
    p_peer->set_index_id(7ul);

    auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
    REQUIRE(diff_opt);

    auto &diff = diff_opt.value();
    REQUIRE(diff->apply(*cluster));

    auto folder_info_peer_new = folder->get_folder_infos().by_device(*peer_device);
    REQUIRE(folder_info_peer_new);
    REQUIRE(folder_info_peer_new == folder_info_peer);
    REQUIRE(folder_info_peer_new->get_file_infos().size() == 0);
    CHECK(folder_info_peer_new->get_index() == 7ul);
    CHECK(folder_info_peer_new->get_max_sequence() == p_peer->max_sequence());

    CHECK(folder_info_peer->get_file_infos().size() == 0);
    CHECK(fi_peer1->use_count() == 1);
    CHECK(fi_peer2->use_count() == 1);

    CHECK(blocks_map.size() == 1);
    CHECK(blocks_map.get(b1->get_hash()));

    bool visited = false;
    auto visitor = my_cluster_update_visitor_t([&](auto &diff) { visited = true; });
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
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();

    cluster->get_folders().put(folder);

    auto folder_info_my = folder_info_ptr_t();
    auto folder_info_peer = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(5ul);
        db_fi.set_max_sequence(10l);
        folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }

    auto cc = std::make_unique<proto::ClusterConfig>();
    auto p_folder = cc->add_folders();
    p_folder->set_id(std::string(folder->get_id()));
    p_folder->set_label(std::string(folder->get_label()));
    auto p_peer = p_folder->add_devices();
    p_peer->set_id(std::string(peer_id.get_sha256()));
    p_peer->set_name(std::string(peer_device->get_name()));
    p_peer->set_max_sequence(123456u);
    p_peer->set_index_id(7ul);

    auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
    REQUIRE(diff_opt);
    auto &diff = diff_opt.value();
    REQUIRE(diff->apply(*cluster));
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
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();

    cluster->get_folders().put(folder);

    auto folder_info_my = folder_info_ptr_t();
    auto folder_info_peer = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(5ul);
        db_fi.set_max_sequence(10l);
        folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(6ul);
        db_fi.set_max_sequence(0l);
        folder_info_peer = folder_info_t::create(sequencer->next_uuid(), db_fi, peer_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }
    folder->get_folder_infos().put(folder_info_my);
    folder->get_folder_infos().put(folder_info_peer);

    auto cc = std::make_unique<proto::ClusterConfig>();
    auto p_folder = cc->add_folders();
    p_folder->set_id(std::string(folder->get_id()));
    p_folder->set_label(std::string(folder->get_label()));
    auto p_peer_1 = p_folder->add_devices();
    p_peer_1->set_id(std::string(peer_id_1.get_sha256()));
    p_peer_1->set_name(std::string(peer_device->get_name()));
    p_peer_1->set_max_sequence(123456u);
    p_peer_1->set_index_id(7ul);

    auto p_peer_2 = p_folder->add_devices();
    p_peer_2->set_id(std::string(peer_id_2.get_sha256()));
    p_peer_2->set_name(std::string(peer_device->get_name()));
    p_peer_2->set_max_sequence(123456u);
    p_peer_2->set_index_id(7ul);

    auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
    REQUIRE(diff_opt);
}

TEST_CASE("cluster update nothing shared", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id_1 =
        device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_id_2 =
        device_id_t::from_string("EAMTZPW-Q4QYERN-D57DHFS-AUP2OMG-PAHOR3R-ZWLKGAA-WQC5SVW-UJ5NXQA").value();

    auto peer_device = device_t::create(peer_id_1, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);
    auto &blocks_map = cluster->get_blocks();

    auto bi1 = proto::BlockInfo();
    bi1.set_size(5);
    bi1.set_hash(utils::sha256_digest("12345").value());
    auto b1 = block_info_t::create(bi1).assume_value();
    blocks_map.put(b1);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();

    cluster->get_folders().put(folder);

    auto folder_info_my = folder_info_ptr_t();
    auto folder_info_peer = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(5ul);
        db_fi.set_max_sequence(10l);
        folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(6ul);
        db_fi.set_max_sequence(0l);
        folder_info_peer = folder_info_t::create(sequencer->next_uuid(), db_fi, peer_device, folder).value();
        folder->get_folder_infos().put(folder_info_peer);

        proto::FileInfo pr_fi_peer;
        pr_fi_peer.set_name("a/c.txt");
        pr_fi_peer.set_size(5ul);
        pr_fi_peer.set_block_size(5ul);
        auto fi_peer = file_info_t::create(sequencer->next_uuid(), pr_fi_peer, folder_info_peer).value();
        folder_info_peer->add(fi_peer, false);
        fi_peer->assign_block(b1, 0);

        REQUIRE(folder_info_peer->get_file_infos().size() == 1);
    }

    auto cc = std::make_unique<proto::ClusterConfig>();
    auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
    REQUIRE(diff_opt);
    auto opt = diff_opt.value()->apply(*cluster);
    REQUIRE(opt);

    CHECK(blocks_map.size() == 0);
    CHECK(folder_info_peer->get_file_infos().size() == 0);
    CHECK(folder->is_shared_with(*peer_device));
    CHECK(folder->is_shared_with(*peer_device)->get_file_infos().size() == 0);
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

TEST_CASE("cluster update with remote folders", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id_1 =
        device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id_1, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();

    cluster->get_folders().put(folder);

    auto folder_info_my = folder_info_ptr_t();
    auto folder_info_peer = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(5ul);
        db_fi.set_max_sequence(10l);
        folder_info_my = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(6ul);
        db_fi.set_max_sequence(0l);
        folder_info_peer = folder_info_t::create(sequencer->next_uuid(), db_fi, peer_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }
    folder->get_folder_infos().put(folder_info_my);
    folder->get_folder_infos().put(folder_info_peer);

    auto cc = std::make_unique<proto::ClusterConfig>();
    auto p_folder = cc->add_folders();
    p_folder->set_id(std::string(folder->get_id()));
    p_folder->set_label(std::string(folder->get_label()));
    auto p_peer_1 = p_folder->add_devices();
    p_peer_1->set_id(std::string(peer_id_1.get_sha256()));
    p_peer_1->set_name(std::string(peer_device->get_name()));
    p_peer_1->set_max_sequence(123456u);
    p_peer_1->set_index_id(7ul);

    auto p_peer_my = p_folder->add_devices();
    p_peer_my->set_id(std::string(my_id.get_sha256()));
    p_peer_my->set_name(std::string(my_device->get_name()));
    p_peer_my->set_max_sequence(3);
    p_peer_my->set_index_id(5ul);

    auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
    REQUIRE(diff_opt);

    auto opt = diff_opt.value()->apply(*cluster);
    REQUIRE(opt);

    auto remote_folder = peer_device->get_remote_folder_infos().by_folder(*folder);
    REQUIRE(remote_folder);
    CHECK(remote_folder->get_index() == 5ul);
    CHECK(remote_folder->get_max_sequence() == 3);

    SECTION("unshare by peer") {
        auto cc = std::make_unique<proto::ClusterConfig>();
        diff_opt = diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer_device, *cc);
        REQUIRE(diff_opt);

        auto &diff = diff_opt.value();
        REQUIRE(diff->apply(*cluster));

        CHECK(peer_device->get_remote_folder_infos().size() == 0);
        auto fi = folder->get_folder_infos().by_device(*peer_device);
        // we are still sharing the folder with peer
        CHECK(fi);
    }
}

int _init() {
    utils::set_default("trace");
    return 1;
}

static int v = _init();
