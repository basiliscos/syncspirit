#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

template <typename F> struct my_cluster_update_visitor_t : diff::cluster_visitor_t {
    F fn;
    bool remove_diff = false;

    my_cluster_update_visitor_t(F &&fn_) : fn{std::forward<F>(fn_)} {}

    outcome::result<void> operator()(const diff::peer::cluster_update_t &diff) noexcept override { return fn(diff); }
    outcome::result<void> operator()(const diff::peer::cluster_remove_t &diff) noexcept override {
        remove_diff = true;
        return outcome::success();
    }
};

TEST_CASE("cluster update, new folder", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
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
        auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *peer_device, *cc);
        REQUIRE(diff_opt);

        auto &diff = diff_opt.value();
        auto r_a = diff->apply(*cluster);
        CHECK(r_a);

        std::vector<proto::Folder> unknown;
        auto visitor = my_cluster_update_visitor_t([&](auto &diff) {
            unknown = diff.unknown_folders;
            return outcome::success();
        });
        auto r_v = diff->visit(visitor);
        REQUIRE(r_v);
        REQUIRE(unknown.size() == 1);
        REQUIRE(unknown[0].id() == folder.id());
        REQUIRE(unknown[0].label() == folder.label());
    }

    SECTION("existing folder") {
        db::Folder db_folder;
        db_folder.set_id("1234-5678");
        db_folder.set_label("my-label");
        db_folder.set_path("/my/path");
        auto folder = folder_t::create(cluster->next_uuid(), db_folder).value();

        cluster->get_folders().put(folder);

        auto folder_info_my = folder_info_ptr_t();
        auto folder_info_peer = folder_info_ptr_t();
        {
            db::FolderInfo db_fi;
            db_fi.set_index_id(5ul);
            db_fi.set_max_sequence(10l);
            folder_info_my = folder_info_t::create(cluster->next_uuid(), db_fi, my_device, folder).value();
            folder->get_folder_infos().put(folder_info_my);
        }
        {
            db::FolderInfo db_fi;
            db_fi.set_index_id(6ul);
            db_fi.set_max_sequence(10l);
            folder_info_peer = folder_info_t::create(cluster->next_uuid(), db_fi, peer_device, folder).value();
            folder->get_folder_infos().put(folder_info_my);
        }
        folder->get_folder_infos().put(folder_info_my);
        folder->get_folder_infos().put(folder_info_peer);

        auto cc = std::make_unique<proto::ClusterConfig>();
        auto p_folder = cc->add_folders();
        p_folder->set_id(std::string(folder->get_id()));
        p_folder->set_label(std::string(folder->get_label()));
        auto p_peer = p_folder->add_devices();
        p_peer->set_id(std::string(peer_id.get_sha256()));
        p_peer->set_name(std::string(peer_device->get_name()));

        SECTION("nothing changed") {
            p_peer->set_max_sequence(folder_info_peer->get_max_sequence());
            p_peer->set_index_id(folder_info_peer->get_index());

            auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *peer_device, *cc);
            REQUIRE(diff_opt);

            auto &diff = diff_opt.value();
            auto r_a = diff->apply(*cluster);
            CHECK(r_a);
            CHECK(folder_info_peer->is_actual());

            bool visited = false;
            auto visitor = my_cluster_update_visitor_t([&](auto &diff) {
                visited = true;
                CHECK(diff.reset_folders.size() == 0);
                CHECK(diff.updated_folders.size() == 0);
                return outcome::success();
            });
            auto r_v = diff->visit(visitor);
            REQUIRE(r_v);
            REQUIRE(visited);
        }

        SECTION("max sequence increased") {
            p_peer->set_max_sequence(folder_info_peer->get_max_sequence() + 1);
            p_peer->set_index_id(folder_info_peer->get_index());

            auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *peer_device, *cc);
            REQUIRE(diff_opt);

            auto &diff = diff_opt.value();
            auto r_a = diff->apply(*cluster);
            CHECK(!folder_info_peer->is_actual());
            CHECK(r_a);

            bool visited = false;
            auto visitor = my_cluster_update_visitor_t([&](auto &diff) {
                visited = true;
                CHECK(diff.reset_folders.size() == 0);
                REQUIRE(diff.updated_folders.size() == 1);
                auto &info = diff.updated_folders[0];
                CHECK(info.folder_id == folder->get_id());
                CHECK(info.device.id() == peer_device->device_id().get_sha256());
                return outcome::success();
            });
            auto r_v = diff->visit(visitor);
            REQUIRE(r_v);
            REQUIRE(visited);
        }
    }

    SECTION("reset index") {
        db::Folder db_folder;
        db_folder.set_id("1234-5678");
        db_folder.set_label("my-label");
        db_folder.set_path("/my/path");
        auto folder = folder_t::create(cluster->next_uuid(), db_folder).value();

        cluster->get_folders().put(folder);

        auto folder_info_my = folder_info_ptr_t();
        auto folder_info_peer = folder_info_ptr_t();
        {
            db::FolderInfo db_fi;
            db_fi.set_index_id(5ul);
            db_fi.set_max_sequence(10l);
            folder_info_my = folder_info_t::create(cluster->next_uuid(), db_fi, my_device, folder).value();
            folder->get_folder_infos().put(folder_info_my);
        }
        {
            db::FolderInfo db_fi;
            db_fi.set_index_id(6ul);
            db_fi.set_max_sequence(10l);
            folder_info_peer = folder_info_t::create(cluster->next_uuid(), db_fi, peer_device, folder).value();
            folder->get_folder_infos().put(folder_info_my);
        }
        folder->get_folder_infos().put(folder_info_my);
        folder->get_folder_infos().put(folder_info_peer);

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

            auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *peer_device, *cc);
            REQUIRE(diff_opt);

            auto &diff = diff_opt.value();
            auto r_a = diff->apply(*cluster);
            REQUIRE(r_a);
            auto fi = folder->get_folder_infos().by_device(peer_device);
            REQUIRE(fi->get_index() == 7ul);
            REQUIRE(fi->get_max_sequence() == 0);

            bool visited = false;
            auto visitor = my_cluster_update_visitor_t([&](auto &diff) {
                visited = true;
                CHECK(diff.reset_folders.size() == 1);
                CHECK(diff.updated_folders.size() == 0);
                return outcome::success();
            });
            auto r_v = diff->visit(visitor);
            REQUIRE(r_v);
            REQUIRE(visited);
        }
    }
}

TEST_CASE("cluster update, reset folder", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");
    auto folder = folder_t::create(cluster->next_uuid(), db_folder).value();

    cluster->get_folders().put(folder);

    auto folder_info_my = folder_info_ptr_t();
    auto folder_info_peer = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(5ul);
        db_fi.set_max_sequence(10l);
        folder_info_my = folder_info_t::create(cluster->next_uuid(), db_fi, my_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
    }
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(6ul);
        db_fi.set_max_sequence(0l);
        folder_info_peer = folder_info_t::create(cluster->next_uuid(), db_fi, peer_device, folder).value();
        folder->get_folder_infos().put(folder_info_my);
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
    auto fi_my = file_info_t::create(cluster->next_uuid(), pr_fi_my, folder_info_my).value();
    folder_info_my->get_file_infos().put(fi_my);

    proto::FileInfo pr_fi_peer1;
    pr_fi_peer1.set_name("a/c.txt");
    pr_fi_peer1.set_size(5ul);
    pr_fi_peer1.set_block_size(5ul);
    auto fi_peer1 = file_info_t::create(cluster->next_uuid(), pr_fi_peer1, folder_info_peer).value();
    folder_info_peer->get_file_infos().put(fi_peer1);
    REQUIRE(folder_info_peer->get_file_infos().size() == 1);

    proto::FileInfo pr_fi_peer2;
    pr_fi_peer2.set_name("a/d.txt");
    pr_fi_peer2.set_size(10ul);
    pr_fi_peer2.set_block_size(5ul);
    auto fi_peer2 = file_info_t::create(cluster->next_uuid(), pr_fi_peer2, folder_info_peer).value();
    folder_info_peer->get_file_infos().put(fi_peer2);
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

    auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *peer_device, *cc);
    REQUIRE(diff_opt);

    auto &diff = diff_opt.value();
    REQUIRE(diff->apply(*cluster));

    auto folder_info_peer_new = folder->get_folder_infos().by_device(peer_device);
    REQUIRE(folder_info_peer_new);
    REQUIRE(folder_info_peer_new != folder_info_peer);
    REQUIRE(folder_info_peer_new->get_file_infos().size() == 0);
    CHECK(folder_info_peer_new->get_index() == 7ul);
    CHECK(folder_info_peer_new->get_max_sequence() == 0);

    CHECK(folder_info_peer->use_count() == 1);
    CHECK(fi_peer1->use_count() == 2);
    CHECK(fi_peer2->use_count() == 2);

    CHECK(blocks_map.size() == 1);
    CHECK(blocks_map.get(b1->get_hash()));

    bool visited = false;
    auto visitor = my_cluster_update_visitor_t([&](auto &diff) {
        visited = true;
        REQUIRE(diff.reset_folders.size() == 1);
        REQUIRE(diff.reset_folders[0].folder_id == folder->get_id());
        REQUIRE(diff.reset_folders[0].device.id() == peer_id.get_sha256());
        CHECK(diff.updated_folders.size() == 0);

        auto &blocks = diff.removed_blocks;
        REQUIRE(blocks.size() == 2);
        return outcome::success();
    });
    auto r_v = diff->visit(visitor);
    REQUIRE(r_v);
    REQUIRE(visited);
    CHECK(visitor.remove_diff);
}

TEST_CASE("cluster update for a folder, which was not shared", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db_folder.set_id("1234-5678");
    db_folder.set_label("my-label");
    db_folder.set_path("/my/path");
    auto folder = folder_t::create(cluster->next_uuid(), db_folder).value();

    cluster->get_folders().put(folder);

    auto folder_info_my = folder_info_ptr_t();
    auto folder_info_peer = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db_fi.set_index_id(5ul);
        db_fi.set_max_sequence(10l);
        folder_info_my = folder_info_t::create(cluster->next_uuid(), db_fi, my_device, folder).value();
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

    auto diff_opt = diff::peer::cluster_update_t::create(*cluster, *peer_device, *cc);
    REQUIRE(!diff_opt);
}
