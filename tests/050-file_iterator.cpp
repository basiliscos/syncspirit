#include "catch.hpp"
#include "test-utils.h"
#include "model/cluster.h"
#include "model/misc/file_iterator.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/clone_file.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/peer/update_folder.h"
#include "model/diff/aggregate.h"

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

    auto file_iterator = file_iterator_ptr_t();
    auto next = [&](bool reset = false) -> file_info_ptr_t {
        if (reset) {
            file_iterator = new file_iterator_t(*cluster, peer_device);
        }
        if (file_iterator) {
            return file_iterator->next();
        }
        return {};
    };

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
        CHECK(!next());
        CHECK(!next(true));
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

    SECTION("file locking") {
        auto file = idx.add_files();
        file->set_name("a.txt");
        file->set_sequence(10ul);

        diff = diff::peer::update_folder_t::create(*cluster, *peer_device, idx).value();
        REQUIRE(diff->apply(*cluster));
        auto peer_folder = folder->get_folder_infos().by_device(peer_device);
        auto peer_file = peer_folder->get_file_infos().by_name("a.txt");

        peer_file->lock();
        auto f = next(true);
        REQUIRE(!f);

        peer_file->unlock();
        f = next(true);
        REQUIRE(f);
    }

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
                auto f1 = next(true);
                REQUIRE(f1);
                CHECK(f1->get_name() == "a.txt");

                auto f2 = next();
                REQUIRE(f2);
                CHECK(f2->get_name() == "b.txt");

                REQUIRE(!next());
            }

            SECTION("appending already visited file") {
                auto f1 = next(true);
                REQUIRE(f1);
                CHECK(f1->get_name() == "a.txt");
                file_iterator->renew(*f1);

                auto f2 = next();
                REQUIRE(f2);
                CHECK(f2->get_name() == "b.txt");

                REQUIRE(!next());
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

                auto f2 = next(true);
                REQUIRE(f2);
                CHECK(f2->get_name() == "b.txt");

                REQUIRE(!next());
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

            auto f = next(true);
            REQUIRE(f);
            CHECK(f->get_name() == "a.txt");
            REQUIRE(!next());
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

            auto f = next(true);
            REQUIRE(f);
            CHECK(f->get_name() == "a.txt");
            REQUIRE(!next());
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
            REQUIRE(!next(true));
        }
    }

    SECTION("file priorities") {
        auto file_1 = idx.add_files();
        file_1->set_name("a.txt");
        file_1->set_sequence(10ul);
        file_1->set_size(10ul);
        file_1->set_block_size(5ul);
        auto version_1 = file_1->mutable_version();
        auto counter_1 = version_1->add_counters();
        counter_1->set_id(14ul);
        counter_1->set_value(1ul);

        auto file_2 = idx.add_files();
        file_2->set_name("b.txt");
        file_2->set_sequence(9ul);
        file_2->set_size(10ul);
        file_2->set_block_size(5ul);
        auto version_2 = file_2->mutable_version();
        auto counter_2 = version_2->add_counters();
        counter_2->set_id(15ul);
        counter_2->set_value(1ul);

        diff = diff::peer::update_folder_t::create(*cluster, *peer_device, idx).value();
        REQUIRE(diff->apply(*cluster));

        auto peer_folder = folder->get_folder_infos().by_device(peer_device);
        auto &peer_files = peer_folder->get_file_infos();
        auto f1 = peer_files.by_name(file_1->name());
        auto f2 = peer_files.by_name(file_2->name());

        SECTION("non-downloaded file takes priority over non-existing") {
            diff = new diff::modify::clone_file_t(*f2);
            REQUIRE(diff->apply(*cluster));
            REQUIRE(next(true) == f2);
            REQUIRE(next(false) == f1);
            REQUIRE(!next(false));
        }

        SECTION("partly-downloaded file takes priority over non-downloaded") {
            auto &blocks_map = cluster->get_blocks();
            diff = new diff::modify::clone_file_t(*f2);
            REQUIRE(diff->apply(*cluster));

            diff = new diff::modify::clone_file_t(*f1);
            REQUIRE(diff->apply(*cluster));

            auto f2_local = f2->local_file();
            REQUIRE(f2_local);

            auto b = proto::BlockInfo();
            auto bi = model::block_info_t::create(b).value();
            blocks_map.put(bi);
            f2_local->assign_block(bi, 0);
            f2_local->mark_local_available(0ul);

            REQUIRE(next(true) == f2);
            REQUIRE(next(false) == f1);
            REQUIRE(!next(false));
        }
    }
}

TEST_CASE("file iterator for 2 folders", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto file_iterator = file_iterator_ptr_t();
    auto next = [&](bool reset = false) -> file_info_ptr_t {
        if (reset) {
            file_iterator = new file_iterator_t(*cluster, peer_device);
        }
        if (file_iterator && *file_iterator) {
            return file_iterator->next();
        }
        return {};
    };

    auto &folders = cluster->get_folders();
    db::Folder db_folder1;
    db_folder1.set_id("1234");
    db_folder1.set_label("my-label-1");
    db_folder1.set_path("/my/path");

    db::Folder db_folder2;
    db_folder2.set_id("5678");
    db_folder2.set_label("my-label-2");
    db_folder2.set_path("/my/path");

    auto diffs = diff::aggregate_t::diffs_t{};
    diffs.push_back(new diff::modify::create_folder_t(db_folder1));
    diffs.push_back(new diff::modify::create_folder_t(db_folder2));
    diffs.push_back(new diff::modify::share_folder_t(peer_id.get_sha256(), db_folder1.id()));
    diffs.push_back(new diff::modify::share_folder_t(peer_id.get_sha256(), db_folder2.id()));

    auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
    REQUIRE(diff->apply(*cluster));
    auto folder1 = folders.by_id(db_folder1.id());
    auto folder2 = folders.by_id(db_folder2.id());

    auto cc = std::make_unique<proto::ClusterConfig>();
    auto p_folder1 = cc->add_folders();
    p_folder1->set_id(std::string(folder1->get_id()));
    p_folder1->set_label(std::string(folder1->get_label()));

    auto p_peer1 = p_folder1->add_devices();
    p_peer1->set_id(std::string(peer_id.get_sha256()));
    p_peer1->set_name(std::string(peer_device->get_name()));
    p_peer1->set_max_sequence(10u);
    p_peer1->set_index_id(123u);

    auto p_folder2 = cc->add_folders();
    p_folder2->set_id(std::string(folder2->get_id()));
    p_folder2->set_label(std::string(folder2->get_label()));

    auto p_peer2 = p_folder2->add_devices();
    p_peer2->set_id(std::string(peer_id.get_sha256()));
    p_peer2->set_name(std::string(peer_device->get_name()));
    p_peer2->set_max_sequence(11u);
    p_peer2->set_index_id(1234u);

    diff = diff::peer::cluster_update_t::create(*cluster, *peer_device, *cc).value();
    REQUIRE(diff->apply(*cluster));

    proto::Index idx1;
    idx1.set_folder(db_folder1.id());

    auto file1 = idx1.add_files();
    file1->set_name("a.txt");
    file1->set_sequence(10ul);

    diff = diff::peer::update_folder_t::create(*cluster, *peer_device, idx1).value();
    REQUIRE(diff->apply(*cluster));

    proto::Index idx2;
    idx2.set_folder(db_folder2.id());

    auto file2 = idx2.add_files();
    file2->set_name("b.txt");
    file2->set_sequence(11ul);

    diff = diff::peer::update_folder_t::create(*cluster, *peer_device, idx2).value();
    REQUIRE(diff->apply(*cluster));

    auto files = std::unordered_set<std::string>{};
    auto f = next(true);
    REQUIRE(f);
    files.emplace(f->get_full_name());

    f = next();
    REQUIRE(f);
    files.emplace(f->get_full_name());

    REQUIRE(!next());
    CHECK(files.size() == 2);
    CHECK(files.count("my-label-1/a.txt"));
    CHECK(files.count("my-label-2/b.txt"));
}
