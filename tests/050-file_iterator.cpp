#include "catch.hpp"
#include "test-utils.h"
#include "model/cluster.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/peer/update_folder.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

TEST_CASE("file iterator", "[model]") {
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

    auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
    REQUIRE(diff->apply(*cluster));
    auto folder = folders.by_id(db_folder.id());

    diff = diff::cluster_diff_ptr_t(new diff::modify::share_folder_t(peer_id.get_sha256(), db_folder.id()));
    REQUIRE(diff->apply(*cluster));

    SECTION("check when no files") {
        CHECK(!cluster->next_file(peer_device));
        CHECK(!cluster->next_file(peer_device, true));
    }

    auto cc = std::make_unique<proto::ClusterConfig>();
    auto p_folder = cc->add_folders();
    p_folder->set_id(std::string(folder->get_id()));
    p_folder->set_label(std::string(folder->get_label()));
    auto p_peer = p_folder->add_devices();
    p_peer->set_id(std::string(peer_id.get_sha256()));
    p_peer->set_name(std::string(peer_device->get_name()));
    p_peer->set_max_sequence(10u);
    p_peer->set_index_id(123u);

    diff = diff::peer::cluster_update_t::create(*cluster, *peer_device, *cc).value();
    REQUIRE(diff->apply(*cluster));

    proto::Index idx;
    idx.set_folder(db_folder.id());

    SECTION("2 files at peer") {
        auto file_1 = idx.add_files();
        file_1->set_name("a.txt");
        file_1->set_sequence(10ul);

        SECTION("simple_cases") {
            auto file_2 = idx.add_files();
            file_2->set_name("b.txt");
            file_2->set_sequence(9ul);

            diff = diff::peer::update_folder_t::create(*cluster, *peer_device, idx).value();
            REQUIRE(diff->apply(*cluster));

            SECTION("files are missing at my side") {
                auto f1 = cluster->next_file(peer_device, true);
                REQUIRE(f1);
                CHECK(f1->get_name() == "a.txt");

                auto f2 = cluster->next_file(peer_device);
                REQUIRE(f2);
                CHECK(f2->get_name() == "b.txt");

                REQUIRE(!cluster->next_file(peer_device));
            }

            SECTION("one file is already exists on my side") {
                auto &folder_infos = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                auto my_folder = folder_infos.by_device(my_device);
                auto pr_file = proto::FileInfo();
                pr_file.set_name("a.txt");
                auto my_file = file_info_t::create(cluster->next_uuid(), pr_file, my_folder).value();
                my_folder->add(my_file);

                auto peer_folder = folder_infos.by_device(peer_device);
                REQUIRE(peer_folder->get_file_infos().size() == 2);

                auto f2 = cluster->next_file(peer_device, true);
                REQUIRE(f2);
                CHECK(f2->get_name() == "b.txt");

                REQUIRE(!cluster->next_file(peer_device));
            }
        }

        SECTION("a file on peer side is newer then on my") {
            auto oth_version = file_1->mutable_version();
            auto counter = oth_version->add_counters();
            counter->set_id(12345ul);
            counter->set_value(1233ul);

            diff = diff::peer::update_folder_t::create(*cluster, *peer_device, idx).value();
            REQUIRE(diff->apply(*cluster));

            auto &folder_infos = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
            proto::Vector my_version;
            auto my_folder = folder_infos.by_device(my_device);
            auto pr_file = proto::FileInfo();
            pr_file.set_name("a.txt");
            my_folder->add(file_info_t::create(cluster->next_uuid(), pr_file, my_folder).value());

            auto f = cluster->next_file(peer_device, true);
            REQUIRE(f);
            CHECK(f->get_name() == "a.txt");
            REQUIRE(!cluster->next_file(peer_device));
        }

        SECTION("a file on peer side is incomplete") {
            file_1->set_size(5ul);
            file_1->set_block_size(5ul);
            auto b = file_1->add_blocks();
            b->set_hash("123");

            diff = diff::peer::update_folder_t::create(*cluster, *peer_device, idx).value();
            REQUIRE(diff->apply(*cluster));

            auto &folder_infos = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
            auto my_folder = folder_infos.by_device(my_device);
            my_folder->add(file_info_t::create(cluster->next_uuid(), *file_1, my_folder).value());

            auto f = cluster->next_file(peer_device, true);
            REQUIRE(f);
            CHECK(f->get_name() == "a.txt");
            REQUIRE(!cluster->next_file(peer_device));
        }

        SECTION("folder info is non-actual") {
            file_1->set_size(5ul);
            file_1->set_block_size(5ul);

            diff = diff::peer::update_folder_t::create(*cluster, *peer_device, idx).value();
            REQUIRE(diff->apply(*cluster));

            auto &folder_infos = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
            auto peer_folder = folder_infos.by_device(peer_device);
            peer_folder->add(file_info_t::create(cluster->next_uuid(), *file_1, peer_folder).value());
            peer_folder->set_remote_max_sequence(peer_folder->get_max_sequence() + 1);
            REQUIRE(!peer_folder->is_actual());
            REQUIRE(!cluster->next_file(peer_device, true));
        }
    }
}
