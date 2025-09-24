// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "access.h"
#include "model/diff/peer/cluster_update.h"
#include "test-utils.h"
#include "fs/scan_task.h"
#include "fs/utils.h"
#include "model/misc/sequencer.h"
#include "diff-builder.h"
#include "test_supervisor.h"

#include <boost/nowide/convert.hpp>
#include <vector>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::model;
using namespace syncspirit::fs;

r::pt::time_duration timeout = r::pt::millisec{10};

TEST_CASE("scan_task", "[fs]") {
    auto root_path = unique_path();
    bfs::create_directories(root_path);
    path_guard_t path_quard{root_path};
    auto rw_cache = fs::file_cache_ptr_t(new fs::file_cache_t(5));

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
    db::set_id(db_folder, "some-id");
    db::set_label(db_folder, "my-label");
    db::set_path(db_folder, root_path.string());

    auto folder = folder_t::create(sequencer->next_uuid(), db_folder).value();
    folder->assign_cluster(cluster);
    cluster->get_folders().put(folder);

    db::FolderInfo db_folder_info;
    db::set_index_id(db_folder_info, 1234);
    db::set_max_sequence(db_folder_info, 3);

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

    auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
    auto r = task.advance();
    CHECK(std::get_if<scan_errors_t>(&r));

    auto errs = std::get_if<scan_errors_t>(&r);
    REQUIRE(errs->size() == 1);

    auto &err = errs->at(0);
    CHECK(err.ec);
    CHECK(err.path == root_path);

    auto &seen = task.get_seen_paths();
    REQUIRE(seen.size() == 1);
    CHECK(seen.begin()->first == "");
    CHECK(seen.begin()->second == root_path);
}
#endif

SECTION("no dirs, no files") {
    auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
    auto r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == true);

    r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == false);
}

SECTION("some dirs, no files") {
    auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
    auto dir = root_path / "some-dir";
    bfs::create_directories(dir);
    auto modified = to_unix(bfs::last_write_time(dir));

    auto r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == true);

    r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == true);

    r = task.advance();
    auto *uf = std::get_if<unknown_file_t>(&r);
    REQUIRE(uf);
    CHECK(uf->path.filename() == "some-dir");
    CHECK(proto::get_size(uf->metadata) == 0);
    CHECK(proto::get_type(uf->metadata) == proto::FileInfoType::DIRECTORY);
    CHECK(proto::get_modified_s(uf->metadata) == modified);

    auto status = bfs::status(dir);
#ifndef SYNCSPIRIT_WIN
    auto perms = static_cast<uint32_t>(status.permissions());
    CHECK(proto::get_permissions(uf->metadata) == perms);
#else
    CHECK(proto::get_permissions(uf->metadata) == 0666);
    CHECK(proto::get_no_permissions(uf->metadata) == 1);
#endif

    r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == false);

    auto &seen = task.get_seen_paths();
    CHECK(seen.count("some-dir"));
}

SECTION("no dirs, unknown files") {
    auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
    write_file(root_path / "some-file", "");
    auto r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == true);

    r = task.advance();
    auto *uf = std::get_if<unknown_file_t>(&r);
    REQUIRE(uf);
    CHECK(uf->path.filename() == "some-file");
    CHECK(proto::get_size(uf->metadata) == 0);
    CHECK(proto::get_type(uf->metadata) == proto::FileInfoType::FILE);

    r = task.advance();
    REQUIRE(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == false);

    auto &seen = task.get_seen_paths();
    CHECK(seen.count("some-file"));
}

#ifndef SYNCSPIRIT_WIN
SECTION("no dirs, symlink to non-existing target") {
    auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
    auto file_path = root_path / "symlink";
    bfs::create_symlink(bfs::path("/some/where"), file_path);

    auto r = task.advance();
    CHECK(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == true);

    r = task.advance();
    auto *uf = std::get_if<unknown_file_t>(&r);
    REQUIRE(uf);
    CHECK(uf->path.filename() == "symlink");
    CHECK(proto::get_size(uf->metadata) == 0);
    CHECK(proto::get_type(uf->metadata) == proto::FileInfoType::SYMLINK);
    CHECK(proto::get_no_permissions(uf->metadata));
    CHECK(proto::get_symlink_target(uf->metadata) == "/some/where");

    r = task.advance();
    REQUIRE(std::get_if<bool>(&r));
    CHECK(*std::get_if<bool>(&r) == false);

    auto &seen = task.get_seen_paths();
    CHECK(seen.count("symlink"));
}
#endif
}

SECTION("regular files") {
    auto modified = std::int64_t{1642007468};
    auto pr_file = proto::FileInfo{};
    proto::set_name(pr_file, "a.txt");
    proto::set_sequence(pr_file, 4);

    auto hash = utils::sha256_digest(as_bytes("12345")).value();
    auto bi = proto::BlockInfo();
    proto::set_size(bi, 5);
    proto::set_hash(bi, hash);
    auto block = block_info_t::create(bi).assume_value();

    auto &v = proto::get_version(pr_file);
    auto &counter = proto::add_counters(v);
    proto::set_id(counter, peer_device->device_id().get_uint());
    proto::set_id(counter, 1);

    SECTION("meta is not changed (file)") {
        auto path = root_path / "a.txt";
        write_file(path, "12345");
        bfs::last_write_time(path, from_unix(modified));
        auto status = bfs::status(path);
        auto perms = static_cast<uint32_t>(status.permissions());

        proto::set_block_size(pr_file, 5);
        proto::set_size(pr_file, 5);
        proto::set_modified_s(pr_file, modified);
        proto::set_permissions(pr_file, perms);
        proto::add_blocks(pr_file, bi);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        file->assign_block(block.get(), 0);
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        REQUIRE(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);

        auto &seen = task.get_seen_paths();
        CHECK(seen.count("a.txt"));
    }

    SECTION("meta is not changed (dir)") {
        proto::set_name(pr_file, "a-dir");
        proto::set_type(pr_file, proto::FileInfoType::DIRECTORY);

        auto dir = root_path / "a-dir";
        bfs::create_directories(dir);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == file);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);

        auto &seen = task.get_seen_paths();
        CHECK(seen.count("a-dir"));
    }

    SECTION("meta is not changed (dir + file inside)") {
        auto pr_dir = pr_file;
        proto::set_name(pr_dir, "a-dir-2");
        proto::set_type(pr_dir, proto::FileInfoType::DIRECTORY);
        proto::set_sequence(pr_dir, proto::get_sequence(pr_file) + 1);

        auto dir = root_path / "a-dir-2";
        bfs::create_directories(dir);

        auto path = root_path / "a-dir-2" / "a.txt";
        write_file(path, "12345");
        bfs::last_write_time(path, from_unix(modified));
        auto status = bfs::status(path);
        auto perms = static_cast<uint32_t>(status.permissions());

        proto::set_name(pr_file, "a-dir-2/a.txt");
        proto::set_block_size(pr_file, 5);
        proto::set_size(pr_file, 5);
        proto::set_modified_s(pr_file, modified);
        proto::set_permissions(pr_file, perms);
        proto::add_blocks(pr_file, bi);

        auto info_file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        info_file->assign_block(block.get(), 0);
        REQUIRE(folder_my->add_strict(info_file));

        auto info_dir = file_info_t::create(sequencer->next_uuid(), pr_dir, folder_my).value();
        REQUIRE(folder_my->add_strict(info_dir));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == info_file);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(ref->file == info_dir);

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);

        auto &seen = task.get_seen_paths();
        CHECK(seen.count("a-dir-2"));
        CHECK(seen.count("a-dir-2/a.txt"));
    }

    SECTION("file has been removed") {
        proto::set_block_size(pr_file, 5);
        proto::set_size(pr_file, 5);
        proto::set_modified_s(pr_file, modified);
        proto::add_blocks(pr_file, bi);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        file->assign_block(block.get(), 0);
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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
        auto &seen = task.get_seen_paths();
        CHECK(seen.count(file->get_name()->get_full_name()));
    }

    SECTION("dir has been removed") {
        proto::set_name(pr_file, "a-dir");
        proto::set_type(pr_file, proto::FileInfoType::DIRECTORY);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

        auto &seen = task.get_seen_paths();
        CHECK(seen.count("a-dir"));
    }

    SECTION("removed file does not exist => unchanged meta") {
        proto::set_deleted(pr_file, true);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

        auto &seen = task.get_seen_paths();
        CHECK(seen.count(file->get_name()->get_full_name()));
    }

    SECTION("removed dir does not exist => unchanged meta") {
        proto::set_deleted(pr_file, true);
        proto::set_type(pr_file, proto::FileInfoType::DIRECTORY);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

        auto &seen = task.get_seen_paths();
        CHECK(seen.count(file->get_name()->get_full_name()));
    }

    SECTION("root dir does not exist & deleted file => unchanged meta") {
        proto::set_deleted(pr_file, true);
        folder->set_path(root_path / "subdir");

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

        auto &seen = task.get_seen_paths();
        CHECK(seen.count(file->get_name()->get_full_name()));
    }

    SECTION("meta is changed") {
        auto task = scan_task_ptr_t{};
        auto file = file_info_ptr_t{};

        auto r = scan_result_t{};

        SECTION("file size differs") {
            auto path = root_path / "a.txt";
            write_file(path, "1234");
            bfs::last_write_time(path, from_unix(modified));
            auto status = bfs::status(path);
            auto perms = static_cast<uint32_t>(status.permissions());

            proto::set_block_size(pr_file, 5);
            proto::set_size(pr_file, 5);
            proto::set_modified_s(pr_file, modified);
            proto::set_permissions(pr_file, perms);
            proto::add_blocks(pr_file, bi);

            file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
            file->assign_block(block.get(), 0);
            REQUIRE(folder_my->add_strict(file));

            task = new scan_task_t(cluster, folder->get_id(), rw_cache, config);

            r = task->advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task->advance();
            REQUIRE(std::get_if<changed_meta_t>(&r));
            auto ref = std::get_if<changed_meta_t>(&r);
            CHECK(ref->file == file);
            CHECK(proto::get_size(ref->metadata) == 4);
            CHECK(proto::get_modified_s(ref->metadata) == modified);
        }

        SECTION("modification time differs") {
            auto path = root_path / "a.txt";
            write_file(path, "12345");
            bfs::last_write_time(path, from_unix(modified));
            auto status = bfs::status(path);
            auto perms = static_cast<uint32_t>(status.permissions());

            proto::set_block_size(pr_file, 5);
            proto::set_size(pr_file, 5);
            proto::set_modified_s(pr_file, modified + 1);
            proto::set_permissions(pr_file, perms);
            proto::add_blocks(pr_file, bi);

            file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
            file->assign_block(block.get(), 0);
            REQUIRE(folder_my->add_strict(file));

            task = new scan_task_t(cluster, folder->get_id(), rw_cache, config);

            r = task->advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task->advance();
            REQUIRE(std::get_if<changed_meta_t>(&r));
            auto ref = std::get_if<changed_meta_t>(&r);
            CHECK(ref->file == file);
            CHECK(proto::get_size(ref->metadata) == 5);
            CHECK(proto::get_modified_s(ref->metadata) == modified);
        }

        SECTION("permissions differs") {
            proto::set_block_size(pr_file, 5);
            proto::set_size(pr_file, 5);
            proto::set_modified_s(pr_file, modified);
            proto::set_permissions(pr_file, static_cast<uint32_t>(-1));
            proto::add_blocks(pr_file, bi);

            auto path = root_path / "a.txt";
            write_file(path, "12345");
            bfs::last_write_time(path, from_unix(modified));

            file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
            file->assign_block(block.get(), 0);
            REQUIRE(folder_my->add_strict(file));

            SECTION("permissions are tracked") {
                task = new scan_task_t(cluster, folder->get_id(), rw_cache, config);

                r = task->advance();
                CHECK(std::get_if<bool>(&r));
                CHECK(*std::get_if<bool>(&r) == true);

#ifndef SYNCSPIRIT_WIN
                r = task->advance();
                REQUIRE(std::get_if<changed_meta_t>(&r));
                auto ref = std::get_if<changed_meta_t>(&r);
                CHECK(ref->file == file);
#else
                r = task->advance();
                REQUIRE(std::get_if<unchanged_meta_t>(&r));
                auto ref = std::get_if<unchanged_meta_t>(&r);
                CHECK(ref->file == file);
#endif
            }
            SECTION("permissions are ignored by folder settigns") {
                SECTION("by folder settings") {
                    ((model::folder_data_t *)folder.get())->access<test::to::ignore_permissions>() = true;
                }
                SECTION("by file") {
                    auto &flags = file.get()->access<test::to::flags>();
                    flags = flags | model::file_info_t::f_no_permissions;
                }
                task = new scan_task_t(cluster, folder->get_id(), rw_cache, config);
                r = task->advance();
                CHECK(std::get_if<bool>(&r));
                CHECK(*std::get_if<bool>(&r) == true);

                r = task->advance();
                REQUIRE(std::get_if<unchanged_meta_t>(&r));
                auto ref = std::get_if<unchanged_meta_t>(&r);
                CHECK(ref->file == file);
            }
        }

        r = task->advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);

        auto &seen = task->get_seen_paths();
        CHECK(seen.count(file->get_name()->get_full_name()));
    }

    SECTION("tmp") {
        proto::set_block_size(pr_file, 5);
        proto::set_size(pr_file, 5);
        proto::set_modified_s(pr_file, modified);
        proto::add_blocks(pr_file, bi);

        auto path = root_path / "a.txt.syncspirit-tmp";

        SECTION("size match -> ok, will recalc") {
            write_file(path, "12345");

            auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
            file_peer->assign_block(block.get(), 0);
            REQUIRE(folder_peer->add_strict(file_peer));

            auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

            auto &seen = task.get_seen_paths();
            CHECK(seen.count("a.txt"));
        }

        SECTION("cannot read -> error") {
            write_file(path, "12345");

            auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
            file_peer->assign_block(block.get(), 0);
            REQUIRE(folder_peer->add_strict(file_peer));

            auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
            auto r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            bfs::remove(path);

            r = task.advance();
            REQUIRE(std::get_if<file_error_t>(&r));
            auto ref = std::get_if<file_error_t>(&r);
            CHECK(ref->path == path);
            CHECK(ref->ec);

            r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == false);

            bfs::permissions(path.parent_path(), bfs::perms::all);

            auto &seen = task.get_seen_paths();
            CHECK(seen.count("a.txt"));
        }

        SECTION("file is in rw cache, skip it") {
            bfs::remove_all(path);
            auto path_rel = bfs::path(L"путь") / bfs::path(L"ля-ля.txt");
            auto path_wstr = path_rel.wstring();
            auto path_str = boost::nowide::narrow(path_rel.wstring());
            auto file_path = path.parent_path() / path_rel;

            bfs::create_directories(file_path.parent_path());
            write_file(file_path, "12345");
            proto::set_name(pr_file, path_str);

            auto cache = fs::file_cache_ptr_t(new fs::file_cache_t(50));
            auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
            file_peer->assign_block(block.get(), 0);
            auto ok = folder_peer->add_strict(file_peer);
            REQUIRE(ok);
            auto file_raw = fs::file_t::open_write(file_peer, *folder_peer).value();
            auto file = fs::file_ptr_t(new fs::file_t(std::move(file_raw)));
            cache->put(file);
            REQUIRE(cache->get(file_path));

            auto task = scan_task_t(cluster, folder->get_id(), cache, config);
            auto r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task.advance();
            REQUIRE(std::get_if<unknown_file_t>(&r));
            auto &uf = std::get<unknown_file_t>(r);
            CHECK(uf.path.filename() == file_path.parent_path().filename());

            r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == false);

            auto &seen = task.get_seen_paths();
            CHECK(seen.count(bfs::path(path_wstr).generic_string()));
            bfs::remove_all(file_path);
        }

        SECTION("size mismatch -> remove & ignore") {
            write_file(path, "123456");

            auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
            file_peer->assign_block(block.get(), 0);
            REQUIRE(folder_peer->add_strict(file_peer));

            // check that local files will not be considered sa removed
            auto file_my = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
            REQUIRE(folder_my->add_strict(file_my));

            auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

            auto &seen = task.get_seen_paths();
            CHECK(seen.count("a.txt"));
        }

        SECTION("size mismatch for global source -> remove & ignore") {
            write_file(path, "123456");

            auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
            file_peer->assign_block(block.get(), 0);
            REQUIRE(folder_peer->add_strict(file_peer));

            proto::set_size(pr_file, file_peer->get_size() + 10);
            proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 2));
            proto::add_blocks(pr_file, bi);
            proto::add_blocks(pr_file, bi);

            auto file_peer2 = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer2).value();
            file_peer2->assign_block(block.get(), 0);
            file_peer2->assign_block(block.get(), 1);
            file_peer2->assign_block(block.get(), 2);
            REQUIRE(folder_peer2->add_strict(file_peer2));

            auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

            auto &seen = task.get_seen_paths();
            CHECK(seen.count("a.txt"));
        }

        SECTION("no source -> remove") {
            write_file(path, "123456");

            auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

            auto &seen = task.get_seen_paths();
            CHECK(!seen.count("a.txt"));
        }
    }
    SECTION("tmp & non-tmp: both are returned") {

        auto path = root_path / "a.txt";
        auto path_tmp = root_path / "a.txt.syncspirit-tmp";
        write_file(path, "12345");
        write_file(path_tmp, "12345");
        bfs::last_write_time(path, from_unix(modified));
        auto status = bfs::status(path);
        auto perms = static_cast<uint32_t>(status.permissions());

        proto::set_block_size(pr_file, 5);
        proto::set_size(pr_file, 5);
        proto::set_modified_s(pr_file, modified);
        proto::set_permissions(pr_file, perms);
        proto::add_blocks(pr_file, bi);
        auto file_my = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        file_my->assign_block(block.get(), 0);

        proto::set_id(counter, 10);
        auto file_peer = file_info_t::create(sequencer->next_uuid(), pr_file, folder_peer).value();
        file_peer->assign_block(block.get(), 0);
        REQUIRE(folder_my->add_strict(file_my));
        REQUIRE(folder_peer->add_strict(file_peer));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

        auto &seen = task.get_seen_paths();
        CHECK(seen.count(path.filename().string()));
        CHECK(seen.count(path_tmp.filename().string()));
    }

    SECTION("cannot read file error") {
        proto::set_name(pr_file, "a.txt");
        auto path = root_path / "a.txt";
        write_file(path, "12345");

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

        auto &seen = task.get_seen_paths();
        CHECK(seen.count(path.filename().string()));
    }

    SECTION("cannot read dir, error") {
        proto::set_name(pr_file, "some/a.txt");
        auto path = root_path / "some" / "a.txt";
        auto parent = path.parent_path();
        write_file(path, "12345");
        sys::error_code ec;
        bfs::permissions(parent, bfs::perms::none, ec);
        bfs::permissions(path, bfs::perms::owner_read, ec);
        if (ec) {
            auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
            REQUIRE(folder_my->add_strict(file));

            auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
            auto r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task.advance();
            REQUIRE(std::get_if<scan_errors_t>(&r));
            auto errs = std::get_if<scan_errors_t>(&r);
            REQUIRE(errs);
            REQUIRE(errs->size() == 1);
            REQUIRE(errs->at(0).path == parent);
            REQUIRE(errs->at(0).ec);

            r = task.advance();
            auto *uf = std::get_if<unknown_file_t>(&r);
            REQUIRE(uf);
            CHECK(uf->path.filename() == "some");
            CHECK(proto::get_size(uf->metadata) == 0);
            CHECK(proto::get_type(uf->metadata) == proto::FileInfoType::DIRECTORY);

            r = task.advance();
            REQUIRE(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == false);
            bfs::permissions(parent, bfs::perms::all);

            auto &seen = task.get_seen_paths();
            CHECK(seen.count("some"));
            CHECK(seen.count("some/a.txt"));
        }
        bfs::permissions(parent, bfs::perms::all, ec);
        bfs::permissions(path, bfs::perms::all, ec);
    }
}

SECTION("symlink file") {
    auto modified = std::time_t{1642007468};
    auto pr_file = proto::FileInfo{};

    proto::set_name(pr_file, "a.txt");
    proto::set_sequence(pr_file, 4);
    proto::set_type(pr_file, proto::FileInfoType::SYMLINK);
    proto::set_symlink_target(pr_file, "b.txt");

    auto &v = proto::get_version(pr_file);
    proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 1));

    SECTION("symlink does not exists") {
        proto::set_modified_s(pr_file, modified);

        auto path = root_path / "a.txt";
        auto target = bfs::path("b.txt");
#ifndef SYNCSPIRIT_WIN
        bfs::create_symlink(target, path);
#endif

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

        auto &seen = task.get_seen_paths();
        CHECK(seen.count("a.txt"));
    }
    SECTION("symlink does exists") {
        proto::set_modified_s(pr_file, modified);
        proto::set_symlink_target(pr_file, "b");

        auto path = root_path / "a.txt";
        auto target = bfs::path("b");
#ifndef SYNCSPIRIT_WIN
        bfs::create_symlink(target, path);
#endif
        bfs::create_directories(root_path / target);

        auto pr_dir = proto::FileInfo{};
        proto::set_name(pr_dir, "b");
        proto::set_sequence(pr_dir, proto::get_sequence(pr_file) + 1);
        proto::set_type(pr_dir, proto::FileInfoType::DIRECTORY);
        proto::set_version(pr_dir, v);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        auto dir = file_info_t::create(sequencer->next_uuid(), pr_dir, folder_my).value();
        REQUIRE(folder_my->add_strict(file));
        REQUIRE(folder_my->add_strict(dir));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
        auto r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

#ifdef SYNCSPIRIT_WIN
        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(((ref->file == dir) || (ref->file == file)));
#else
        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        auto ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(((ref->file == dir) || (ref->file == file)));

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == true);
#endif

        r = task.advance();
        REQUIRE(std::get_if<unchanged_meta_t>(&r));
        ref = std::get_if<unchanged_meta_t>(&r);
        CHECK(((ref->file == dir) || (ref->file == file)));

        r = task.advance();
        CHECK(std::get_if<bool>(&r));
        CHECK(*std::get_if<bool>(&r) == false);

        auto &seen = task.get_seen_paths();
        CHECK(seen.count("a.txt"));
    }

    SECTION("symlink to root") {
        proto::set_modified_s(pr_file, modified);
        proto::set_symlink_target(pr_file, "/");

        auto path = root_path / "a.txt";
        auto target = bfs::path("/");
#ifndef SYNCSPIRIT_WIN
        bfs::create_symlink(target, path);
#endif

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

        auto &seen = task.get_seen_paths();
        CHECK(seen.count("a.txt"));
    }

#ifndef SYNCSPIRIT_WIN
    SECTION("symlink points to something different") {
        proto::set_modified_s(pr_file, modified);

        auto path = root_path / "a.txt";
        auto target = bfs::path("c.txt");
        bfs::create_symlink(target, path);

        auto file = file_info_t::create(sequencer->next_uuid(), pr_file, folder_my).value();
        REQUIRE(folder_my->add_strict(file));

        auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
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

        auto &seen = task.get_seen_paths();
        CHECK(seen.count("a.txt"));
    }
#endif
}
}

TEST_CASE("scan_task diffs aggregation, guard", "[fs]") {
    struct my_supervisor_t : supervisor_t {
        using supervisor_t::supervisor_t;

        void on_model_update(model::message::model_update_t &msg) noexcept override {
            ++model_updates;
            supervisor_t::on_model_update(msg);
        }

        outcome::result<void> operator()(const diff::peer::cluster_update_t &diff, void *custom) noexcept override {
            ++cluster_updates;
            return diff.visit_next(*this, custom);
        }

        int model_updates = 0;
        int cluster_updates = 0;
    };

    static constexpr int SIZE = 41;
    auto root_path = unique_path();
    bfs::create_directories(root_path);
    path_guard_t path_quard{root_path};
    auto rw_cache = fs::file_cache_ptr_t(new fs::file_cache_t(5));

    config::fs_config_t config{3600, 10, 1024 * 1024, 5};
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto db_folder = db::Folder();
    db::set_id(db_folder, "some-id");
    db::set_label(db_folder, "my-label");
    db::set_path(db_folder, root_path.string());

    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder(db_folder).apply());

    auto folders = cluster->get_folders();
    auto folder = folders.by_id("some-id");

    r::system_context_t ctx;
    auto sup = ctx.create_supervisor<my_supervisor_t>().timeout(timeout).create_registry().finish();
    sup->cluster = cluster;
    sup->do_process();

    auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);
    {
        auto guard = task.guard(*sup, sup->get_address());
        guard.send_by_force();
        auto sha256 = peer_id.get_sha256();
        for (int i = 0; i < SIZE; ++i) {
            auto diff = builder.configure_cluster(sha256, {}).finish().extract();
            task.push(diff.get());
        }
    }

    sup->do_process();
    CHECK(sup->model_updates == (41 / 5 + 1));
    CHECK(sup->cluster_updates == SIZE);

    sup->do_shutdown();
    sup->do_process();
}

TEST_CASE("scan_task order", "[fs]") {
    using paths_t = std::vector<std::string>;
    auto root_path = unique_path();
    bfs::create_directories(root_path);
    path_guard_t path_quard{root_path};
    auto rw_cache = fs::file_cache_ptr_t(new fs::file_cache_t(5));

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    auto builder = diff_builder_t(*cluster);

    auto db_folder = db::Folder();
    db::set_id(db_folder, "some-id");
    db::set_label(db_folder, "my-label");
    db::set_path(db_folder, root_path.string());

    REQUIRE(builder.upsert_folder(db_folder).apply());

    bfs::create_directory(root_path / "a");
    bfs::create_directory(root_path / "a" / "c");
    bfs::create_directory(root_path / "b");
    bfs::create_directory(root_path / "d");
    bfs::create_directory(root_path / "d" / "d1");
    bfs::create_directory(root_path / "d" / "d2");
    write_file(root_path / "x.bin", "");
    write_file(root_path / "y.bin", "");
    write_file(root_path / "a/file.bin", "");
    write_file(root_path / "a/c/file_2.bin", "");
    write_file(root_path / "d/d1/file_3.bin", "");

    auto folders = cluster->get_folders();
    auto folder = folders.by_id("some-id");
    auto folder_my = folder->get_folder_infos().by_device(*my_device);

    config::fs_config_t config{3600, 10, 1024 * 1024, 100};
    auto task = scan_task_t(cluster, folder->get_id(), rw_cache, config);

    bool try_next = true;
    auto visited_paths = paths_t();
    while (try_next) {
        auto r = task.advance();
        std::visit(
            [&](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, bool>) {
                    try_next = arg;
                } else if constexpr (std::is_same_v<T, unknown_file_t>) {
                    auto p = relativize(arg.path, root_path);
                    auto str = p.string();
                    std::replace(str.begin(), str.end(), '\\', '/');
                    visited_paths.emplace_back(str);
                } else {
                    try_next = false;
                }
            },
            r);
    }

    auto expected = paths_t{"x.bin", "y.bin",           "a/file.bin", "a/c/file_2.bin", "a/c", "a",
                            "b",     "d/d1/file_3.bin", "d/d1",       "d/d2",           "d"};
    CHECK(visited_paths == expected);
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
