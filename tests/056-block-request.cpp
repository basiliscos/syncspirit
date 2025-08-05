// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "test-utils.h"
#include "diff-builder.h"
#include "access.h"
#include "model/cluster.h"
#include <boost/nowide/convert.hpp>
#include "net/messages.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::model;

TEST_CASE("block request", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);

    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());

    auto folder = cluster->get_folders().by_id("1234-5678");
    auto folder_my = folder->get_folder_infos().by_device(*my_device);

    auto pr_file_1 = proto::FileInfo();
    proto::set_name(pr_file_1, "a.bin");
    proto::set_block_size(pr_file_1, 5);
    proto::set_size(pr_file_1, 5);

    auto b_1_hash = utils::sha256_digest(as_bytes("12345")).value();
    auto &b_1 = proto::add_blocks(pr_file_1);
    proto::set_hash(b_1, b_1_hash);
    proto::set_size(b_1, 5);

    auto bi_1 = block_info_t::create(b_1).value();
    auto &blocks_map = cluster->get_blocks();
    blocks_map.put(bi_1);

    auto &v1 = proto::get_version(pr_file_1);
    proto::add_counters(v1, proto::Counter(my_device->device_id().get_uint(), 1));

    auto pr_file_2 = proto::FileInfo();
    proto::set_name(pr_file_2, "b.bin");
    proto::set_block_size(pr_file_2, 5);
    proto::set_size(pr_file_2, 10);

    auto b_21_hash = utils::sha256_digest(as_bytes("67890")).value();
    auto &b_21 = proto::add_blocks(pr_file_2);
    proto::set_hash(b_21, b_21_hash);
    proto::set_size(b_21, 5);

    auto bi_2 = block_info_t::create(b_21).value();
    blocks_map.put(bi_2);

    auto &b_22 = proto::add_blocks(pr_file_2);
    proto::set_hash(b_22, b_1_hash);
    proto::set_size(b_22, 5);
    proto::set_offset(b_22, 5);

    auto &v2 = proto::get_version(pr_file_2);
    proto::add_counters(v2, proto::Counter(my_device->device_id().get_uint(), 1));

    CHECK(proto::get_blocks_size(pr_file_1) == 1);
    CHECK(proto::get_blocks_size(pr_file_2) == 2);
    REQUIRE(builder.local_update(folder->get_id(), pr_file_1).apply());
    REQUIRE(builder.local_update(folder->get_id(), pr_file_2).apply());

    auto f1 = folder_my->get_file_infos().by_name("a.bin");
    REQUIRE(f1->get_blocks().size() == 1);

    auto f2 = folder_my->get_file_infos().by_name("b.bin");
    REQUIRE(f2->get_blocks().size() == 2);

    using request_t = syncspirit::net::payload::block_request_t;

    auto r1 = request_t(f1, 0);
    CHECK(r1.block_offset == 0);
    CHECK(r1.block_size == 5);

    auto r2 = request_t(f2, 1);
    CHECK(r2.block_offset == 5);
    CHECK(r2.block_size == 5);

    CHECK(r1.block_hash == r2.block_hash);
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
