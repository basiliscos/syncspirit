// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "model/diff/load/interrupt.h"
#include "test-utils.h"
#include "diff-builder.h"

#include "model/cluster.h"
#include "model/misc/sequencer.h"
#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

TEST_CASE("scan_start", "[model]") {
    test::init_logging();

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();

    auto my_device = device_ptr_t{};
    my_device = new model::local_device_t(my_id, "my-device", "my-device");

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    auto &devices = cluster->get_devices();
    devices.put(my_device);

    auto &blocks_map = cluster->get_blocks();
    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("f1", "some/path-1", "my-label-1").apply());

    auto folder = cluster->get_folders().by_id("f1");
    auto folder_my = folder->get_folder_infos().by_device(*my_device);

    proto::FileInfo pr_fi_1;
    proto::set_name(pr_fi_1, "a.bin");
    proto::set_block_size(pr_fi_1, 5ul);
    auto &v_1 = proto::get_version(pr_fi_1);
    proto::add_counters(v_1, proto::Counter(my_id.get_uint(), 0));

    proto::FileInfo pr_fi_2;
    proto::set_name(pr_fi_2, "b.bin");
    auto &v_2 = proto::get_version(pr_fi_1);
    proto::add_counters(v_2, proto::Counter(my_id.get_uint(), 0));

    REQUIRE(builder.local_update("f1", pr_fi_1).local_update("f1", pr_fi_2).apply());

    auto &file_infos = folder_my->get_file_infos();
    auto f1 = file_infos.by_name("a.bin");
    auto f2 = file_infos.by_name("b.bin");

    REQUIRE(f1->is_local());
    REQUIRE(f2->is_local());

    REQUIRE(builder.scan_start("f1").apply());
    CHECK(!f1->is_local());
    CHECK(!f2->is_local());
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
