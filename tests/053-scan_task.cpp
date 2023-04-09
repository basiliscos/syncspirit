// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "fs/scan_task.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::model;
using namespace syncspirit::fs;

TEST_CASE("scan_task", "[fs]") {
    utils::set_default("trace");

    auto root_path = bfs::unique_path();
    bfs::create_directories(root_path);
    path_guard_t path_quard{root_path};

    config::fs_config_t config{3600, 10};
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto db_folder = db::Folder();
    db_folder.set_id("some-id");
    db_folder.set_label("zzz");
    db_folder.set_path(root_path.string());
    auto folder = folder_t::create(cluster->next_uuid(), db_folder).value();
    cluster->get_folders().put(folder);

    db::FolderInfo db_folder_info;
    db_folder_info.set_index_id(1234);
    db_folder_info.set_max_sequence(3);
    auto folder_my = folder_info_t::create(cluster->next_uuid(), db_folder_info, my_device, folder).value();
    auto folder_peer = folder_info_t::create(cluster->next_uuid(), db_folder_info, peer_device, folder).value();
    folder->get_folder_infos().put(folder_my);
    folder->get_folder_infos().put(folder_peer);

    SECTION("without files") {
        SECTION("no permissions to read dir => err") {
            bfs::permissions(root_path, bfs::perms::no_perms);

            auto folder_info = folder_info_t::create(cluster->next_uuid(), db_folder_info, my_device, folder).value();
            folder->get_folder_infos().put(folder_info);

            auto task = scan_task_t(cluster, folder->get_id(), config);
            auto r = task.advance();
            CHECK(std::get_if<io_errors_t>(&r));

            auto errs = std::get_if<io_errors_t>(&r);
            REQUIRE(errs->size() == 1);

            auto &err = errs->at(0);
            CHECK(err.ec);
            CHECK(err.path == root_path);
        }

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

        SECTION("no dirs, symlinks") {
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

            r = task.advance();
            REQUIRE(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == false);
        }
    }

    SECTION("files") {
        auto modified = std::time_t{1642007468};
        auto pr_file = proto::FileInfo{};
        pr_file.set_name("a.txt");
        pr_file.set_sequence(2);
        auto version = pr_file.mutable_version();
        auto counter = version->add_counters();
        counter->set_id(1);
        counter->set_value(peer_device->as_uint());

        SECTION("meta is not changed") {
            pr_file.set_block_size(5);
            pr_file.set_size(5);
            pr_file.set_modified_s(modified);

            auto path = root_path / "a.txt";
            write_file(path, "12345");
            bfs::last_write_time(path, modified);

            auto file = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
            folder_my->add(file, false);

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

        SECTION("file has been removed") {
            pr_file.set_block_size(5);
            pr_file.set_size(5);
            pr_file.set_modified_s(modified);

            auto file = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
            folder_my->add(file, false);

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

            auto file = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
            folder_my->add(file, false);

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

            auto file = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
            folder_my->add(file, false);

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

            SECTION("file size differs") {
                pr_file.set_block_size(5);
                pr_file.set_size(6);
                pr_file.set_modified_s(modified);

                auto path = root_path / "a.txt";
                write_file(path, "12345");
                bfs::last_write_time(path, modified);

                file = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
                folder_my->add(file, false);
            }

            SECTION("modification time differs") {
                pr_file.set_block_size(5);
                pr_file.set_size(5);
                pr_file.set_modified_s(modified + 1);

                auto path = root_path / "a.txt";
                write_file(path, "12345");
                bfs::last_write_time(path, modified);

                file = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
                folder_my->add(file, false);
            }
            task = new scan_task_t(cluster, folder->get_id(), config);
            auto r = task->advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            r = task->advance();
            REQUIRE(std::get_if<changed_meta_t>(&r));
            auto ref = std::get_if<changed_meta_t>(&r);
            CHECK(ref->file == file);

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

                auto file_my = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
                auto file_peer = file_info_t::create(cluster->next_uuid(), pr_file, folder_peer).value();
                folder_my->add(file_my, false);
                folder_peer->add(file_peer, false);
                file_my->set_source(file_peer);

                auto task = scan_task_t(cluster, folder->get_id(), config);
                auto r = task.advance();
                CHECK(std::get_if<bool>(&r));
                CHECK(*std::get_if<bool>(&r) == true);

                r = task.advance();
                REQUIRE(std::get_if<incomplete_t>(&r));
                auto ref = std::get_if<incomplete_t>(&r);
                CHECK(ref->file);

                r = task.advance();
                CHECK(std::get_if<bool>(&r));
                CHECK(*std::get_if<bool>(&r) == false);
            }

            SECTION("source is missing") {
                write_file(path, "12345");

                auto file_my = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
                folder_my->add(file_my, false);

                auto task = scan_task_t(cluster, folder->get_id(), config);
                auto r = task.advance();
                CHECK(std::get_if<bool>(&r));
                CHECK(*std::get_if<bool>(&r) == true);

                r = task.advance();
                CHECK(std::get_if<incomplete_removed_t>(&r));
                CHECK(std::get_if<incomplete_removed_t>(&r)->file == file_my);

                r = task.advance();
                CHECK(std::get_if<bool>(&r));
                CHECK(*std::get_if<bool>(&r) == false);
            }

            SECTION("size mismatch -> remove & ignore") {
                write_file(path, "123456");

                auto file_my = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
                auto file_peer = file_info_t::create(cluster->next_uuid(), pr_file, folder_peer).value();
                folder_my->add(file_my, false);
                folder_peer->add(file_peer, false);
                file_my->set_source(file_peer);

                auto task = scan_task_t(cluster, folder->get_id(), config);
                auto r = task.advance();
                CHECK(std::get_if<bool>(&r));
                CHECK(*std::get_if<bool>(&r) == true);

                r = task.advance();
                CHECK(std::get_if<incomplete_removed_t>(&r));
                CHECK(std::get_if<incomplete_removed_t>(&r)->file == file_my);

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

            auto file_my = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
            counter->set_id(10);
            auto file_peer = file_info_t::create(cluster->next_uuid(), pr_file, folder_peer).value();
            folder_my->add(file_my, false);
            folder_peer->add(file_peer, false);
            file_my->set_source(file_peer);

            auto path = root_path / "a.txt";
            auto path_tmp = root_path / "a.txt.syncspirit-tmp";
            write_file(path, "12345");
            write_file(path_tmp, "12345");
            bfs::last_write_time(path, modified);

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
            r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == false);
        }

        SECTION("cannot read file error") {
            pr_file.set_name("a.txt");
            auto path = root_path / "a.txt";
            write_file(path, "12345");

            auto file = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
            folder_my->add(file, false);

            auto task = scan_task_t(cluster, folder->get_id(), config);
            auto r = task.advance();
            CHECK(std::get_if<bool>(&r));
            CHECK(*std::get_if<bool>(&r) == true);

            bfs::remove(path);
            r = task.advance();
            REQUIRE(std::get_if<file_error_t>(&r));
            auto err = std::get_if<file_error_t>(&r);
            REQUIRE(err->file == file);
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
            bfs::permissions(parent, bfs::perms::no_perms, ec);
            bfs::permissions(path, bfs::perms::owner_read, ec);
            if (ec) {
                auto file = file_info_t::create(cluster->next_uuid(), pr_file, folder_my).value();
                folder_my->add(file, false);

                auto task = scan_task_t(cluster, folder->get_id(), config);
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
                REQUIRE(std::get_if<bool>(&r));
                CHECK(*std::get_if<bool>(&r) == false);
                bfs::permissions(parent, bfs::perms::all_all);
            }
        }
    }
}
