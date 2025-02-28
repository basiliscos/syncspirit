// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include "test-utils.h"
#include "model/cluster.h"
#include "model/file_info.h"
#include "model/misc/sequencer.h"

using namespace syncspirit;
using namespace syncspirit::model;

using Catch::Matchers::Matches;

TEST_CASE("file-info", "[model]") {
    auto sequencer = make_sequencer(4);
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    db::Folder db_folder;
    db::set_id(db_folder, "1234-5678");
    db::set_label(db_folder, "my-label");
    db::set_path(db_folder, "/my/path");
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();

    db::FolderInfo db_folder_info;
    db::set_index_id(db_folder_info, 2);
    db::set_max_sequence(db_folder_info, 3);
    auto folder_info = folder_info_t::create(sequencer->next_uuid(), db_folder_info, my_device, folder).value();

    proto::FileInfo pr_fi;
    auto name = "a/b.txt";
    proto::set_name(pr_fi, name);
    proto::set_size(pr_fi, 55ul);
    proto::set_block_size(pr_fi, 5ul);
    proto::set_sequence(pr_fi, 6);
    proto::set_modified_s(pr_fi, 1734680712);
    proto::add_counters(proto::get_version(pr_fi), proto::Counter(my_device->device_id().get_uint(), 0));

    auto fi = file_info_t::create(sequencer->next_uuid(), pr_fi, folder_info).value();
    auto map = file_infos_map_t{};

    map.put(fi);
    CHECK(map.by_name(name) == fi);
    CHECK(map.by_sequence(proto::get_sequence(pr_fi)) == fi);

    auto [begin, end] = map.range(0, 10);
    CHECK(std::distance(begin, end) == 1);
    CHECK(begin->item == fi);

    map.remove(fi);
    fi->set_sequence(10);

    map.put(fi);
    CHECK(map.by_name(proto::get_name(pr_fi)) == fi);
    CHECK(map.by_sequence(10) == fi);
    CHECK(!map.by_sequence(proto::get_sequence(pr_fi)));

    auto conflict_name = fi->make_conflicting_name();
    REQUIRE_THAT(conflict_name, Matches("a.b.sync-conflict-202412(\\d){2}-(\\d){6}-KHQNO2S.txt"));
}
