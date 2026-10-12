// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include "test-utils.h"
#include "model/file_info.h"
#include "model/misc/sequencer.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::test;

using F = model::presence_t::features_t;

using Catch::Matchers::Matches;

TEST_CASE("entities & presences", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    auto sequencer = make_sequencer(4);

    db::Folder db_folder;
    db::set_id(db_folder, "some-id");
    db::set_label(db_folder, "my-label");
    db::set_path(db_folder, "/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();
    folder->assign_cluster(cluster);
    cluster->get_folders().put(folder);

    auto folder_info = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db::set_index_id(db_fi, 5ul);
        db::set_max_sequence(db_fi, 10l);
        folder_info = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
    }


    proto::FileInfo pr_dir;
    proto::set_name(pr_dir, "file.bin");
    proto::set_sequence(pr_dir, 11);
    proto::add_counters(proto::get_version(pr_dir), proto::Counter(my_device->device_id().get_uint(), 0));
    auto fi = file_info_t::create(sequencer->next_uuid(), pr_dir, folder_info).value();
    CHECK(fi->get_features() & F::cluster);


    SECTION("add_relaxed") {
        folder_info->add_relaxed(fi.get());
    }
    SECTION("add_strict") {
        folder_info->add_relaxed(fi.get());
    }
    CHECK(folder_info->get_max_sequence() == 11);
    REQUIRE(folder_info->get_children().size() == 1);
    folder_info->clear();
    CHECK(folder_info->get_children().size() == 0);

}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
