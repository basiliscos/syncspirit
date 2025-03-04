// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "model/cluster.h"
#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

TEST_CASE("file_info_t::local_file", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto folder_my = folder_infos.by_device(*my_device);
    auto folder_peer = folder_infos.by_device(*peer_device);

    auto pr_file = proto::FileInfo();
    pr_file.set_name("a.txt");
    auto version = pr_file.mutable_version();
    auto c1 = version->add_counters();
    c1->set_id(1);
    c1->set_value(1);

    SECTION("no local file") {
        auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
        CHECK(!file_peer->local_file());
    }

    SECTION("there is identical local file") {
        pr_file.set_sequence(folder_my->get_max_sequence() + 1);
        auto file_my = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file_my));

        pr_file.set_sequence(folder_peer->get_max_sequence() + 1);
        auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
        REQUIRE(folder_peer->add_strict(file_peer));

        auto lf = file_peer->local_file();
        REQUIRE(lf);
        REQUIRE(lf == file_my);
    }
}

TEST_CASE("file_info_t::check_consistency", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto folder_my = folder_infos.by_device(*my_device);

    auto pr_file = proto::FileInfo();
    pr_file.set_name("a.txt");
    pr_file.set_block_size(5);
    pr_file.set_size(5);

    auto b = pr_file.add_blocks();
    b->set_hash(utils::sha256_digest("12345").value());
    b->set_weak_hash(555);
    b->set_size(5ul);
    auto bi = block_info_t::create(*b).value();
    auto &blocks_map = cluster->get_blocks();
    blocks_map.put(bi);
    auto bbb = blocks_map.get(b->hash());
    REQUIRE(bbb);

    REQUIRE(builder.local_update(folder->get_id(), pr_file).apply());
    auto file_my = folder_my->get_file_infos().by_name(pr_file.name());
    CHECK(file_my->get_size() == 5);
}

TEST_CASE("file_info_t::create, inconsistent source") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);
    builder.upsert_folder("1234-5678", "some/path", "my-label");
    REQUIRE(builder.apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto folder_my = folder_infos.by_device(*my_device);

    auto pr_block = proto::BlockInfo();
    pr_block.set_size(131072);

    auto pr_file = proto::FileInfo();
    pr_file.set_name("a.txt");
    *pr_file.add_blocks() = pr_block;
    pr_file.set_block_size(131072);
    pr_file.set_size(0);
    pr_file.mutable_version()->add_counters()->set_id(my_device->device_id().get_uint());

    auto my_file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
    CHECK(my_file->get_block_size() == 0);
    CHECK(my_file->get_blocks().size() == 0);
}
