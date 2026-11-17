// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include "test-utils.h"
#include "model/file_info.h"
#include "model/misc/sequencer.h"
#include "model/misc/folder_info_iterator.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::test;

using F = model::presence_t::features_t;
using FT = proto::FileInfoType;

using Catch::Matchers::Matches;

TEST_CASE("entities & best of presences", "[model]") {
    auto local_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto local_device = device_t::create(local_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(local_device, 1));
    auto sequencer = make_sequencer(4);

    cluster->get_devices().put(local_device);
    cluster->get_devices().put(peer_device);

    db::Folder db_folder;
    db::set_id(db_folder, "1234-5678");
    db::set_label(db_folder, "my-label");
    db::set_path(db_folder, "/my/path");

    auto uuid = sequencer->next_uuid();
    auto folder = folder_t::create(uuid, db_folder).value();
    folder->assign_cluster(cluster);
    cluster->get_folders().put(folder);

    db::FolderInfo db_folder_info(2, 3, {});
    auto local_folder = folder_info_t::create(sequencer->next_uuid(), db_folder_info, local_device, folder).value();
    auto peer_folder = folder_info_t::create(sequencer->next_uuid(), db_folder_info, peer_device, folder).value();
    auto sequence = std::int64_t{1};
    auto mk_file = [&](const device_id_t &device, model::folder_info_t &folder_info, std::string_view name = "a.bin",
                       std::uint64_t value = 10) -> model::file_info_ptr_t {
        proto::FileInfo pr;
        proto::set_name(pr, name);
        proto::set_sequence(pr, ++sequence);
        auto device_id = device.get_uint();
        proto::add_counters(proto::get_version(pr), proto::Counter(device_id, value));
        auto fi = file_info_t::create(sequencer->next_uuid(), pr, local_folder).value();
        return fi;
    };

    SECTION("empty folder-info") {
        CHECK(folder->get_best() == nullptr);
        CHECK(folder->get_best() == nullptr);
    }

    auto local_file = mk_file(local_id, *local_folder);
    local_folder->add_file(*local_file);
    auto file_entity = local_file->get_entity();
    REQUIRE(file_entity);
    CHECK(file_entity->get_best() == local_file.get());
    CHECK(file_entity->get_best() == local_file.get());

    SECTION("same peer file") {
        auto peer_file = mk_file(local_id, *peer_folder);
        peer_folder->add_file(*peer_file);
        CHECK(file_entity->get_best() == local_file.get());
    }
    SECTION("worse peer file") {
        auto peer_file = mk_file(local_id, *peer_folder, "a.bin", 9);
        peer_folder->add_file(*peer_file);
        CHECK(file_entity->get_best() == local_file.get());
    }
    SECTION("better peer file") {
        auto peer_file = mk_file(local_id, *peer_folder, "a.bin", 11);
        peer_folder->add_file(*peer_file);
        CHECK(file_entity->get_best() == peer_file.get());

        SECTION("rm peer file") {
            peer_file->forget_presence();
            CHECK(file_entity->get_best() == local_file.get());

            // add again
            auto peer_file = mk_file(local_id, *peer_folder, "a.bin", 12);
            peer_folder->add_file(*peer_file);
            CHECK(file_entity->get_best() == peer_file.get());
        }
    }
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
