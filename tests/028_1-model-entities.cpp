// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

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

    auto sequence = std::int64_t{10};
    auto folder_info = folder_info_ptr_t();
    {
        db::FolderInfo db_fi;
        db::set_index_id(db_fi, 5ul);
        db::set_max_sequence(db_fi, sequence);
        folder_info = folder_info_t::create(sequencer->next_uuid(), db_fi, my_device, folder).value();
    }

    auto mk_file= [&](std::string_view name, proto::FileInfoType type = proto::FileInfoType::FILE) -> model::file_info_ptr_t {
        proto::FileInfo pr;
        proto::set_name(pr, name);
        proto::set_sequence(pr, ++sequence);
        proto::set_type(pr, type);
        proto::add_counters(proto::get_version(pr), proto::Counter(my_device->device_id().get_uint(), 0));
        auto fi = file_info_t::create(sequencer->next_uuid(), pr, folder_info).value();
        CHECK(fi->get_features() & F::cluster);
        return fi;
    };

    SECTION("simple addition") {
        auto file = mk_file("file.bin");
        SECTION("add_relaxed") {
            folder_info->add_relaxed(file.get());
        }
        SECTION("add_strict") {
            folder_info->add_relaxed(file.get());
        }
        CHECK(folder_info->get_max_sequence() == sequence);
        REQUIRE(folder_info->get_children().size() == 1);
    }

    SECTION("normalization (sorting)") {
        SECTION("files are sorted") {
            auto f_1 = mk_file("a.bin");
            auto f_2 = mk_file("b.bin");
            auto f_3 = mk_file("c.bin");

            SECTION("inverse order") {
                folder_info->add_relaxed(f_3.get());
                folder_info->add_relaxed(f_2.get());
                folder_info->add_relaxed(f_1.get());
            }
            SECTION("direct order") {
                folder_info->add_relaxed(f_1.get());
                folder_info->add_relaxed(f_2.get());
                folder_info->add_relaxed(f_3.get());
            }

            auto& children = folder_info->get_children();
            REQUIRE(children.size() == 3);
            CHECK(children[0] == f_1.get());
            CHECK(children[1] == f_2.get());
            CHECK(children[2] == f_3.get());
        }
        SECTION("dirs are first") {
            auto f_1 = mk_file("a", FT::FILE);
            auto f_2 = mk_file("b", FT::DIRECTORY);
            auto f_3 = mk_file("c", FT::FILE);
            SECTION("inverse order") {
                folder_info->add_relaxed(f_3.get());
                folder_info->add_relaxed(f_2.get());
                folder_info->add_relaxed(f_1.get());
            }
            SECTION("direct order") {
                folder_info->add_relaxed(f_1.get());
                folder_info->add_relaxed(f_2.get());
                folder_info->add_relaxed(f_3.get());
            }

            auto& children = folder_info->get_children();
            REQUIRE(children.size() == 3);
            CHECK(children[0] == f_2.get());
            CHECK(children[1] == f_1.get());
            CHECK(children[2] == f_3.get());
        }
    }

    SECTION("iteration") {
        SECTION("no files") {
            auto it = folder_info->iterate();
            CHECK(it->next() == nullptr);
            CHECK(it->next() == nullptr);
        }
        SECTION("single file") {
            auto f = mk_file("a", FT::FILE);
            folder_info->add_relaxed(f.get());
            auto it = folder_info->iterate();
            CHECK(it->next() == f);
            CHECK(it->next() == nullptr);
            CHECK(it->get_seen_sequnece() == sequence);
        }
        SECTION("3 flat files") {
            auto f_1 = mk_file("a", FT::DIRECTORY);
            auto f_2 = mk_file("b", FT::DIRECTORY);
            auto f_3 = mk_file("c", FT::DIRECTORY);
            folder_info->add_relaxed(f_3.get());
            folder_info->add_relaxed(f_2.get());
            folder_info->add_relaxed(f_1.get());

            auto it = folder_info->iterate();
            CHECK(it->next() == f_1);
            CHECK(it->next() == f_2);
            CHECK(it->next() == f_3);
            CHECK(it->next() == nullptr);
            CHECK(it->get_seen_sequnece() == sequence);
        }
        SECTION("simple hierarchy (1)") {
            auto f_1 = mk_file("a", FT::DIRECTORY);
            auto f_2 = mk_file("b", FT::DIRECTORY);
            auto f_3 = mk_file("a/a0", FT::DIRECTORY);
            auto f_4 = mk_file("a/a1", FT::DIRECTORY);
            folder_info->add_relaxed(f_1.get());
            folder_info->add_relaxed(f_2.get());
            folder_info->add_relaxed(f_3.get());
            folder_info->add_relaxed(f_4.get());

            auto it = folder_info->iterate();
            CHECK(it->next() == f_1);
            CHECK(it->next() == f_3);
            CHECK(it->next() == f_4);
            CHECK(it->next() == f_2);
            CHECK(it->next() == nullptr);
            CHECK(it->get_seen_sequnece() == sequence);
        }
        SECTION("simple hierarchy (2)") {
            auto f_1 = mk_file("a", FT::DIRECTORY);
            auto f_2 = mk_file("b", FT::DIRECTORY);
            auto f_3 = mk_file("b/b0", FT::DIRECTORY);
            auto f_4 = mk_file("b/b1", FT::DIRECTORY);
            folder_info->add_relaxed(f_1.get());
            folder_info->add_relaxed(f_2.get());
            folder_info->add_relaxed(f_4.get());
            folder_info->add_relaxed(f_3.get());

            auto it = folder_info->iterate();
            CHECK(it->next() == f_1);
            CHECK(it->next() == f_2);
            CHECK(it->next() == f_3);
            CHECK(it->next() == f_4);
            CHECK(it->next() == nullptr);
            CHECK(it->get_seen_sequnece() == sequence);
        }
    }

    folder_info->clear();
    CHECK(folder_info->get_children().size() == 0);

}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
