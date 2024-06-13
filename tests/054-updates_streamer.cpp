// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023-2024 Ivan Baidakou

#include "test-utils.h"
#include "model/misc/updates_streamer.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/share_folder.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::model;

TEST_CASE("updates_streamer", "[model]") {
    utils::set_default("trace");

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1, 1));
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

    auto add_remote = [&](std::uint64_t index, std::int64_t sequence) {
        auto remote_folder = remote_folder_info_t::create(index, sequence, *peer_device, *folder).value();
        peer_device->get_remote_folder_infos().put(remote_folder);
    };

    SECTION("trivial") {
        SECTION("no files") {
            auto streamer = model::updates_streamer_t(*cluster, *peer_device);
            REQUIRE(!streamer);
        }

        add_remote(0, 0);

        SECTION("no files (2)") {
            auto streamer = model::updates_streamer_t(*cluster, *peer_device);
            REQUIRE(!streamer);
        }
    }

    auto my_folder = folder->get_folder_infos().by_device(*my_device);
    auto &my_files = my_folder->get_file_infos();

    int seq = 1;
    auto add_file = [&](const char *name) {
        auto pr_file = proto::FileInfo();
        pr_file.set_name(name);
        pr_file.set_sequence(seq++);
        auto f = file_info_t::create(cluster->next_uuid(), pr_file, my_folder).value();
        my_files.put(f);
        my_folder->set_max_sequence(f->get_sequence());
        return f;
    };

    SECTION("2 files, index mismatch") {
        SECTION("zero sequence") { add_remote(0, 0); }
        SECTION("non-zero sequence") { add_remote(0, seq + 100); }

        auto f1 = add_file("a.txt");
        auto f2 = add_file("b.txt");

        auto streamer = model::updates_streamer_t(*cluster, *peer_device);
        REQUIRE(streamer);
        CHECK(streamer.next() == f1);

        REQUIRE(streamer);
        CHECK(streamer.next() == f2);

        REQUIRE(!streamer);
    }

    SECTION("2 files, index matches, sequence greater") {
        auto f1 = add_file("a.txt");
        auto f2 = add_file("b.txt");

        add_remote(my_folder->get_index(), f2->get_sequence());

        auto streamer = model::updates_streamer_t(*cluster, *peer_device);
        REQUIRE(!streamer);
    }

    SECTION("2 files, index matches, sequence greater") {
        auto f1 = add_file("a.txt");
        auto f2 = add_file("b.txt");

        add_remote(my_folder->get_index(), f1->get_sequence());

        auto streamer = model::updates_streamer_t(*cluster, *peer_device);
        REQUIRE(streamer);
        CHECK(streamer.next() == f2);
        REQUIRE(!streamer);
    }

    SECTION("1 file, streamer is updated lazily") {
        add_remote(0, seq);

        auto streamer = model::updates_streamer_t(*cluster, *peer_device);
        REQUIRE(!streamer);

        auto f1 = add_file("a.txt");
        streamer.on_update(*f1);
        CHECK(streamer.next() == f1);

        REQUIRE(!streamer);
    }

    SECTION("empty streamer ignores updates") {
        add_remote(0, seq);

        auto streamer = model::updates_streamer_t();
        REQUIRE(!streamer);

        auto f1 = add_file("a.txt");
        streamer.on_update(*f1);
        REQUIRE(!streamer);
    }

    SECTION("2 files, streamer is updated") {
        add_remote(0, seq + 100);

        auto f1 = add_file("a.txt");
        auto f2 = add_file("b.txt");

        auto streamer = model::updates_streamer_t(*cluster, *peer_device);
        REQUIRE(streamer);

        f1->set_sequence(++seq);
        my_folder->set_max_sequence(seq);
        streamer.on_update(*f1);

        REQUIRE(streamer.next() == f2);
        REQUIRE(streamer.next() == f1);
        REQUIRE(!streamer);
    }
}
