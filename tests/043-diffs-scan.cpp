// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "test-utils.h"
#include "diff-builder.h"

#include "model/cluster.h"
#include "model/misc/sequencer.h"
#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
auto my_device = device_ptr_t(new model::local_device_t(my_id, "my-device", "my-device"));

TEST_CASE("scan_start, full", "[model]") {
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    auto &devices = cluster->get_devices();
    devices.put(my_device);

    auto &blocks_map = cluster->get_blocks();
    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("f1", "some/path-1", "my-label-1").apply());

    auto folder = cluster->get_folders().by_id("f1");
    auto folder_local = folder->get_folder_info(*my_device);

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

    auto path_1 = utils::path_t::make_generic("a.bin");
    auto path_2 = utils::path_t::make_generic("b.bin");
    auto f1 = folder_local->get_by_path(path_1).first;
    auto f2 = folder_local->get_by_path(path_2).first;

    REQUIRE(f1->is_local());
    REQUIRE(f2->is_local());

    REQUIRE(builder.scan_start("f1").apply());
    CHECK(!f1->is_local());
    CHECK(!f2->is_local());
}

TEST_CASE("scan_start, partial", "[model]") {
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    auto &devices = cluster->get_devices();
    devices.put(my_device);

    auto &blocks_map = cluster->get_blocks();
    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("f1", "some/path-1", "my-label-1").apply());

    auto folder = cluster->get_folders().by_id("f1");
    auto folder_local = folder->get_folder_info(*my_device);

    auto names = {"dir", "dir_x", "dir/sub_dir"};
    for (auto &name : names) {
        proto::FileInfo pr_fi;
        proto::set_name(pr_fi, name);
        proto::set_type(pr_fi, proto::FileInfoType::DIRECTORY);
        auto &v_1 = proto::get_version(pr_fi);
        proto::add_counters(v_1, proto::Counter(my_id.get_uint(), 0));
        REQUIRE(builder.local_update("f1", pr_fi).apply());
        auto path = utils::path_t::make_generic(name);
        auto [f, _] = folder_local->get_by_path(path);
        REQUIRE(f);
        REQUIRE(f->is_local());
    }

    auto path_dir = utils::path_t::make_generic("dir");
    auto path_subdir = utils::path_t::make_generic("dir/sub_dir");
    auto path_dir_x = utils::path_t::make_generic("dir_x");
    REQUIRE(builder.scan_start("f1", path_dir.clone()).apply());
    CHECK(!folder_local->get_by_path(path_dir).first->is_local());
    CHECK(!folder_local->get_by_path(path_subdir).first->is_local());
    CHECK(folder_local->get_by_path(path_dir_x).first->is_local());
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
