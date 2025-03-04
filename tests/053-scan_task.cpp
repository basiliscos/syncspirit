// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "fs/scan_task.h"
#include "fs/utils.h"
#include "model/misc/sequencer.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::model;
using namespace syncspirit::fs;

TEST_CASE("scan_task", "[fs]") {
    test::init_logging();

    auto root_path = unique_path();
    bfs::create_directories(root_path);
    path_guard_t path_quard{root_path};

    config::fs_config_t config{3600, 10, 1024 * 1024, 100};
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto peer2_id = device_id_t::from_string("XBOWTOU-Y7H6RM6-D7WT3UB-7P2DZ5G-R6GNZG6-T5CCG54-SGVF3U5-LBM7RQB").value();
    auto peer2_device = device_t::create(peer2_id, "peer2-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);
    cluster->get_devices().put(peer2_device);

    auto db_folder = db::Folder();
    db_folder.set_id("some-id");
    db_folder.set_label("zzz");
    db_folder.set_path(root_path.string());
    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();
    cluster->get_folders().put(folder);

    db::FolderInfo db_folder_info;
    db_folder_info.set_index_id(1234);
    db_folder_info.set_max_sequence(3);
    auto folder_my = folder_info_t::create(sequencer->next_uuid(), db_folder_info, my_device, folder).value();
    auto folder_peer = folder_info_t::create(sequencer->next_uuid(), db_folder_info, peer_device, folder).value();
    auto folder_peer2 = folder_info_t::create(sequencer->next_uuid(), db_folder_info, peer2_device, folder).value();
    folder->get_folder_infos().put(folder_my);
    folder->get_folder_infos().put(folder_peer);
    folder->get_folder_infos().put(folder_peer2);

    SECTION("without files"){

#ifndef SYNCSPIRIT_WIN
        SECTION("no permissions to read dir => err"){bfs::permissions(root_path, bfs::perms::none);

    auto folder_info = folder_info_t::create(sequencer->next_uuid(), db_folder_info, my_device, folder).value();
    folder->get_folder_infos().put(folder_info);

    auto task = scan_task_t(cluster, folder->get_id(), config);
    auto r = task.advance();
    CHECK(std::get_if<scan_errors_t>(&r));

    auto errs = std::get_if<scan_errors_t>(&r);
    REQUIRE(errs->size() == 1);

    auto &err = errs->at(0);
    CHECK(err.ec);
    CHECK(err.path == root_path);
}
#endif
SECTION("no dirs, no files") {
    auto task = scan_task_t(cluster, folder->get_id(), config);
    auto r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == true);

    r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == false);
}

SECTION("some dirs, no files") {
    auto task = scan_task_t(cluster, folder->get_id(), config);
    auto dir = root_path / "some-dir";
    bfs::create_directories(dir);
    auto r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == true);

    r = task.advance();
    auto *uf = std::get_if<unknown_file_t>(&r);
    REQUIRE(uf);
    CHECK(uf->path.filename() == "some-dir");
    CHECK(uf->metadata.size() == 0);
    CHECK(uf->metadata.type() == proto::FileInfoType::DIRECTORY);

    r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == true);

    r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == false);
}

SECTION("no dirs, unknown files") {
    auto task = scan_task_t(cluster, folder->get_id(), config);
    write_file(root_path / "some-file", "");
    auto r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == true);

    r = task.advance();
    auto *uf = std::get_if<unknown_file_t>(&r);
    REQUIRE(uf);
    CHECK(uf->path.filename() == "some-file");
    CHECK(uf->metadata.size() == 0);
    CHECK(uf->metadata.type() == proto::FileInfoType::FILE);

    r = task.advance();
    REQUIRE(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == false);
}

#ifndef SYNCSPIRIT_WIN
SECTION("no dirs, symlink to non-existing target") {
    auto task = scan_task_t(cluster, folder->get_id(), config);
    auto file_path = root_path / "symlink";
    bfs::create_symlink(bfs::path("/some/where"), file_path);

    auto r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == true);

    r = task.advance();
    auto *uf = std::get_if<unknown_file_t>(&r);
    REQUIRE(uf);
    CHECK(uf->path.filename() == "symlink");
    CHECK(uf->metadata.size() == 0);
    CHECK(uf->metadata.type() == proto::FileInfoType::SYMLINK);
    CHECK(uf->metadata.symlink_target() == "/some/where");

    r = task.advance();
    REQUIRE(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == false);
}
#endif
}

SECTION("regular files") {
    auto modified = std::int64_t{1642007468};
    auto pr_file = proto::FileInfo{};
    pr_file.set_name("a.txt");
    pr_file.set_sequence(4);
    auto version = pr_file.mutable_version();
    auto counter = version->add_counters();
    counter->set_value(1);
    counter->set_id(peer_device->device_id().get_uint());

    SECTION("meta is not changed (file)") {
        pr_file.set_block_size(5);
        pr_file.set_size(5);
        pr_file.set_modified_s(modified);

        auto path = root_path / "a.txt";
        write_file(path, "12345");
        bfs::last_write_time(path, from_unix(modified));
        auto status = bfs::status(path);
        pr_file.set_permissions(static_cast<uint32_t>(status.permissions()));

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("meta is not changed (dir)") {
        pr_file.set_name("a-dir");
        pr_file.set_type(proto::FileInfoType::DIRECTORY);

        auto dir = root_path / "a-dir";
        bfs::create_directories(dir);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("meta is not changed (dir + file inside)") {
        auto pr_dir = pr_file;
        pr_dir.set_name("a-dir-2");
        pr_dir.set_type(proto::FileInfoType::DIRECTORY);
        pr_dir.set_sequence(pr_file.sequence() + 1);

        pr_file.set_block_size(5);
        pr_file.set_size(5);
        pr_file.set_modified_s(modified);
        pr_file.set_name("a-dir-2/a.txt");

        auto dir = root_path / "a-dir-2";
        bfs::create_directories(dir);

        auto path = root_path / "a-dir-2" / "a.txt";
        write_file(path, "12345");
        bfs::last_write_time(path, from_unix(modified));
        auto status = bfs::status(path);
        pr_file.set_permissions(static_cast<uint32_t>(status.permissions()));

        auto info_file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(info_file));

        auto info_dir = file_info_t::create(sequencer->next_uuid(), pr_dir, folder_my).value();
        REQUIRE(folder_my->add_strict(info_dir));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == info_dir);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == info_file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("file has been removed") {
        pr_file.set_block_size(5);
        pr_file.set_size(5);
        pr_file.set_modified_s(modified);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<removed_t>(&r));
        auto ref = std::get_if<removed_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("dir has been removed") {
        pr_file.set_name("a-dir");
        pr_file.set_type(proto::FileInfoType::DIRECTORY);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<removed_t>(&r));
        auto ref = std::get_if<removed_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("removed file does not exist => unchanged meta") {
        pr_file.set_deleted(true);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("removed dir does not exist => unchanged meta") {
        pr_file.set_deleted(true);
        pr_file.set_type(proto::FileInfoType::DIRECTORY);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("root dir does not exist & deleted file => unchanged meta") {
        pr_file.set_deleted(true);
        folder->set_path(root_path / "zzz");

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        REQUIRE(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("meta is changed") {
        auto task = scan_task_ptr_t{};
        auto file = file_info_ptr_t{};

        auto r = scan_result_t{};

        SECTION("file size differs") {
            pr_file.set_block_size(5);
            pr_file.set_size(6);
            pr_file.set_modified_s(modified);

            auto path = root_path / "a.txt";
            write_file(path, "12345");
            bfs::last_write_time(path, from_unix(modified));
            auto status = bfs::status(path);
            pr_file.set_permissions(static_cast<uint32_t>(status.permissions()));

            file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
            REQUIRE(folder_my->add_strict(file));

            task = new scan_task_t(cluster, folder->get_id(), config);

            r = task->advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task->advance();
            REQUIRE(std::get_if<changed_meta_t>(&r));
            auto ref = std::get_if<changed_meta_t>(&r);
            CHECK(ref->file == file);
            CHECK(ref->metadata.size() == 5);
            CHECK(ref->metadata.modified_s() == modified);
        }

        SECTION("modification time differs") {
            pr_file.set_block_size(5);
            pr_file.set_size(5);
            pr_file.set_modified_s(modified + 1);

            auto path = root_path / "a.txt";
            write_file(path, "12345");
            bfs::last_write_time(path, from_unix(modified));
            auto status = bfs::status(path);
            pr_file.set_permissions(static_cast<uint32_t>(status.permissions()));

            file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
            REQUIRE(folder_my->add_strict(file));

            task = new scan_task_t(cluster, folder->get_id(), config);

            r = task->advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task->advance();
            REQUIRE(std::get_if<changed_meta_t>(&r));
            auto ref = std::get_if<changed_meta_t>(&r);
            CHECK(ref->file == file);
            CHECK(ref->metadata.size() == 5);
            CHECK(ref->metadata.modified_s() == modified);
        }

        SECTION("permissions differs") {
            pr_file.set_block_size(5);
            pr_file.set_size(5);
            pr_file.set_modified_s(modified);
            pr_file.set_permissions(static_cast<uint32_t>(-1));

            auto path = root_path / "a.txt";
            write_file(path, "12345");
            bfs::last_write_time(path, from_unix(modified));

            file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
            REQUIRE(folder_my->add_strict(file));

            task = new scan_task_t(cluster, folder->get_id(), config);

            r = task->advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task->advance();
            REQUIRE(std::get_if<changed_meta_t>(&r));
            auto ref = std::get_if<changed_meta_t>(&r);
            CHECK(ref->file == file);
        }

        r = task->advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("tmp") {
        pr_file.set_block_size(5);
        pr_file.set_size(5);
        pr_file.set_modified_s(modified);

        auto path = root_path / "a.txt.syncspirit-tmp";

        SECTION("size match -> ok, will recalc") {
            write_file(path, "12345");

            auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
            REQUIRE(folder_peer->add_strict(file_peer));

            auto task = scan_task_t(cluster, folder->get_id(), config);
            auto r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task.advance();
            REQUIRE(std::get_if<incomplete_t>(&r));
            auto ref = std::get_if<incomplete_t>(&r);
            CHECK(ref->file);
            CHECK(ref->opened_file);

            r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == false);
        }

        SECTION("size mismatch -> remove & ignore") {
            write_file(path, "123456");

            auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
            REQUIRE(folder_peer->add_strict(file_peer));

            auto task = scan_task_t(cluster, folder->get_id(), config);
            auto r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task.advance();
            CHECK(std::get_if<incomplete_removed_t>(&r));
            CHECK(std::get_if<incomplete_removed_t>(&r)->file == file_peer);

            r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == false);
            CHECK(!bfs::exists(path));
        }

        SECTION("size mismatch for global source -> remove & ignore") {
            write_file(path, "123456");

            auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
            REQUIRE(folder_peer->add_strict(file_peer));

            pr_file.set_size(file_peer->get_size() + 10);
            auto c2 = version->add_counters();
            c2->set_id(peer2_device->device_id().get_uint());
            c2->set_value(2);

            auto file_peer2 = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer2).value();
            REQUIRE(folder_peer2->add_strict(file_peer2));

            auto task = scan_task_t(cluster, folder->get_id(), config);
            auto r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task.advance();
            CHECK(std::get_if<incomplete_removed_t>(&r));
            CHECK(std::get_if<incomplete_removed_t>(&r)->file == file_peer2);

            r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == false);
            CHECK(!bfs::exists(path));
        }

        SECTION("no source -> remove") {
            write_file(path, "123456");

            auto task = scan_task_t(cluster, folder->get_id(), config);
            auto r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task.advance();
            CHECK(std::get_if<orphaned_removed_t>(&r));
            CHECK(std::get_if<orphaned_removed_t>(&r)->path == path);

            r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == false);
            CHECK(!bfs::exists(path));
        }
    }
    SECTION("tmp & non-tmp: both are returned") {
        pr_file.set_block_size(5);
        pr_file.set_size(5);
        pr_file.set_modified_s(modified);

        auto path = root_path / "a.txt";
        auto path_tmp = root_path / "a.txt.syncspirit-tmp";
        write_file(path, "12345");
        write_file(path_tmp, "12345");
        bfs::last_write_time(path, from_unix(modified));
        auto status = bfs::status(path);
        pr_file.set_permissions(static_cast<uint32_t>(status.permissions()));

        auto file_my = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        counter->set_id(10);
        auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
        REQUIRE(folder_my->add_strict(file_my));
        REQUIRE(folder_peer->add_strict(file_peer));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        unchanged_meta_t unchanged;
        incomplete_t incomplete;
        for (int i = 0; i < 2; ++i) {
            r = task.advance();
            std::visit(
                [&](auto &&it) {
                    using T = std::decay_t<decltype(it)>;
                    if constexpr (std::is_same_v<T, unchanged_meta_t>) {
                        unchanged = it;
                    } else if constexpr (std::is_same_v<T, incomplete_t>) {
                        incomplete = it;
                    } else {
                        REQUIRE((0 && "unexpected result"));
                    }
                },
                r);
        }
        CHECK(unchanged.file == file_my);
        CHECK(incomplete.file);
        CHECK(incomplete.opened_file);
        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("cannot read file error") {
        pr_file.set_name("a.txt");
        auto path = root_path / "a.txt";
        write_file(path, "12345");

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        bfs::remove(path);
        r = task.advance();
        REQUIRE(std::get_if<file_error_t>(&r));
        auto err = std::get_if<file_error_t>(&r);
        REQUIRE(err->path == path);
        REQUIRE(err->ec);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("cannot read dir, error") {
        pr_file.set_name("some/a.txt");
        auto path = root_path / "some" / "a.txt";
        auto parent = path.parent_path();
        write_file(path, "12345");
        sys::error_code ec;
        bfs::permissions(parent, bfs::perms::none, ec);
        bfs::permissions(path, bfs::perms::owner_read, ec);
        if (ec) {
            auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
            REQUIRE(folder_my->add_strict(file));

            auto task = scan_task_t(cluster, folder->get_id(), config);
            auto r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task.advance();
            auto *uf = std::get_if<unknown_file_t>(&r);
            REQUIRE(uf);
            CHECK(uf->path.filename() == "some");
            CHECK(uf->metadata.size() == 0);
            CHECK(uf->metadata.type() == proto::FileInfoType::DIRECTORY);

            r = task.advance();
            REQUIRE(std::get_if<scan_errors_t>(&r));
            auto errs = std::get_if<scan_errors_t>(&r);
            REQUIRE(errs);
            REQUIRE(errs->size() == 1);
            REQUIRE(errs->at(0).path == parent);
            REQUIRE(errs->at(0).ec);

            r = task.advance();
            REQUIRE(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == false);
            bfs::permissions(parent, bfs::perms::all);
        }
    }
}

#ifndef SYNCSPIRIT_WIN
SECTION("symlink file") {
    auto modified = std::time_t{1642007468};
    auto pr_file = proto::FileInfo{};
    pr_file.set_name("a.txt");
    pr_file.set_sequence(4);
    pr_file.set_type(proto::FileInfoType::SYMLINK);
    pr_file.set_symlink_target("b.txt");
    auto version = pr_file.mutable_version();
    auto counter = version->add_counters();
    counter->set_id(1);
    counter->set_value(peer_device->device_id().get_uint());

    SECTION("symlink does not exists") {
        pr_file.set_modified_s(modified);

        auto path = root_path / "a.txt";
        auto target = bfs::path("b.txt");
        bfs::create_symlink(target, path);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("symlink does exists") {
        pr_file.set_modified_s(modified);
        pr_file.set_symlink_target("b");

        auto path = root_path / "a.txt";
        auto target = bfs::path("b");
        bfs::create_symlink(target, path);
        bfs::create_directories(root_path / target);

        auto pr_dir = proto::FileInfo{};
        pr_dir.set_name("b");
        pr_dir.set_sequence(pr_file.sequence() + 1);
        pr_dir.set_type(proto::FileInfoType::DIRECTORY);
        *pr_dir.mutable_version() = pr_file.version();

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        auto dir = file_info_t::create(sequencer->next_uuid(), pr_dir, folder_my).value();
        REQUIRE(folder_my->add_strict(file));
        REQUIRE(folder_my->add_strict(dir));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(((ref->file == dir) || (ref->file == file)));

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(((ref->file == dir) || (ref->file == file)));

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("symlink to root") {
        pr_file.set_modified_s(modified);
        pr_file.set_symlink_target("/");

        auto path = root_path / "a.txt";
        auto target = bfs::path("/");
        bfs::create_symlink(target, path);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }

    SECTION("symlink points to something different") {
        pr_file.set_modified_s(modified);

        auto path = root_path / "a.txt";
        auto target = bfs::path("c.txt");
        bfs::create_symlink(target, path);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<changed_meta_t>(&r));
        auto ref = std::get_if<changed_meta_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);
    }
}
#endif
}
