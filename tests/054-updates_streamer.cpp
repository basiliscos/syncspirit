// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023-2024 Ivan Baidakou

#include "test-utils.h"
#include "diff-builder.h"
#include "model/misc/updates_streamer.h"

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

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);
    auto builder = diff_builder_t(*cluster);

    auto &folders = cluster->get_folders();
    REQUIRE(builder.upsert_folder("1234-5678", "/my/path").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
    auto folder = folders.by_id("1234-5678");

    auto add_remote = [&](std::uint64_t index, std::int64_t sequence) {
        auto remote_folder = remote_folder_info_t::create(index, sequence, *peer_device, *folder).value();
        peer_device->get_remote_folder_infos().put(remote_folder);
    };

    SECTION("trivial") {
        SECTION("no files") {
            auto streamer = model::updates_streamer_t(*cluster, *peer_device);
            REQUIRE(!streamer.next());
        }

        add_remote(0, 0);

        SECTION("no files (2)") {
            auto streamer = model::updates_streamer_t(*cluster, *peer_device);
            REQUIRE(!streamer.next());
        }
    }

    auto my_folder = folder->get_folder_infos().by_device(*my_device);
    auto &my_files = my_folder->get_file_infos();

    int seq = 1;
    auto add_file = [&](const char *name) {
        auto pr_file = proto::FileInfo();
        pr_file.set_name(name);
        pr_file.set_sequence(seq++);
        pr_file.mutable_version()->add_counters()->set_id(my_device->as_uint());
        auto f = file_info_t::create(sequencer->next_uuid(), pr_file, my_folder).value();
        my_folder->add_strict(f);
        return f;
    };

    SECTION("2 files, index mismatch") {
        SECTION("zero sequence") { add_remote(0, 0); }
        SECTION("non-zero sequence") { add_remote(0, seq + 100); }

        auto f1 = add_file("a.txt");
        auto f2 = add_file("b.txt");

        auto streamer = model::updates_streamer_t(*cluster, *peer_device);
        CHECK(streamer.next() == f1);
        CHECK(streamer.next() == f2);
        CHECK(!streamer.next());
    }

    SECTION("2 files, index matches, sequence greater") {
        auto f1 = add_file("a.txt");
        auto f2 = add_file("b.txt");

        add_remote(my_folder->get_index(), f2->get_sequence());

        auto streamer = model::updates_streamer_t(*cluster, *peer_device);
        CHECK(!streamer.next());
    }

    SECTION("2 files, index matches, sequence greater") {
        auto f1 = add_file("a.txt");
        auto f2 = add_file("b.txt");

        add_remote(my_folder->get_index(), f1->get_sequence());

        auto streamer = model::updates_streamer_t(*cluster, *peer_device);
        CHECK(streamer.next() == f2);
        CHECK(!streamer.next());
    }

    SECTION("1 file, streamer is updated lazily") {
        add_remote(0, seq);

        auto streamer = model::updates_streamer_t(*cluster, *peer_device);
        REQUIRE(!streamer.next());

        auto f1 = add_file("a.txt");
        streamer.on_update(*f1);
        CHECK(streamer.next() == f1);
        CHECK(!streamer.next());
    }

    SECTION("empty streamer ignores updates") {
        add_remote(0, seq);
        auto peer_folder = folder->get_folder_infos().by_device(*peer_device);
        REQUIRE(builder.unshare_folder(*peer_folder).apply());

        auto streamer = model::updates_streamer_t(*cluster, *peer_device);
        REQUIRE(!streamer.next());

        auto f1 = add_file("a.txt");
        streamer.on_update(*f1);
        CHECK(!streamer.next());
    }

    SECTION("2 files, streamer is updated") {
        add_remote(0, seq + 100);

        auto f1 = add_file("a.txt");
        auto f2 = add_file("b.txt");

        auto streamer = model::updates_streamer_t(*cluster, *peer_device);

        f1->set_sequence(++seq);
        my_folder->add_strict(f1);

        streamer.on_update(*f1);

        REQUIRE(streamer.next() == f2);
        REQUIRE(streamer.next() == f1);
        REQUIRE(!streamer.next());
    }
}
