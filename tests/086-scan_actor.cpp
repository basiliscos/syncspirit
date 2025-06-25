// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"
#include "managed_hasher.h"
#include "diff-builder.h"

#include "model/cluster.h"
#include "hasher/hasher_proxy_actor.h"
#include "fs/scan_actor.h"
#include "fs/utils.h"
#include "net/names.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::fs;
using namespace syncspirit::hasher;

using fs_time_t = std::filesystem::file_time_type;

struct fixture_t {
    using target_ptr_t = r::intrusive_ptr_t<fs::scan_actor_t>;
    using builder_ptr_t = std::unique_ptr<diff_builder_t>;

    fixture_t() noexcept : root_path{unique_path()}, path_guard{root_path} {
        test::init_logging();
        bfs::create_directory(root_path);
    }

    void run() noexcept {
        auto my_id =
            device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        my_device = device_t::create(my_id, "my-device").value();
        auto peer_id =
            device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
        peer_device = device_t::create(peer_id, "peer-device").value();

        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().make_presentation(true).timeout(timeout).create_registry().finish();
        sup->cluster = cluster;

        auto folder_id = "1234-5678";

        sup->start();
        sup->do_process();
        builder = std::make_unique<diff_builder_t>(*cluster);
        builder->upsert_folder(folder_id, root_path)
            .apply(*sup)
            .share_folder(peer_id.get_sha256(), folder_id)
            .apply(*sup);

        folder = cluster->get_folders().by_id(folder_id);
        folder_info = folder->get_folder_infos().by_device(*my_device);
        files = &folder_info->get_file_infos();
        folder_info_peer = folder->get_folder_infos().by_device(*peer_device);
        files_peer = &folder_info_peer->get_file_infos();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        hasher = sup->create_actor<managed_hasher_t>().index(1).auto_reply(true).timeout(timeout).finish().get();
        auto proxy_addr = sup->create_actor<hasher::hasher_proxy_actor_t>()
                              .timeout(timeout)
                              .hasher_threads(1)
                              .name(net::names::hasher_proxy)
                              .finish()
                              ->get_address();

        sup->do_process();

        auto fs_config = config::fs_config_t{3600, 10, 1024 * 1024, files_scan_iteration_limit};
        rw_cache.reset(new fs::file_cache_t(5));

        target = sup->create_actor<fs::scan_actor_t>()
                     .timeout(timeout)
                     .rw_cache(rw_cache)
                     .cluster(cluster)
                     .sequencer(make_sequencer(77))
                     .fs_config(fs_config)
                     .requested_hashes_limit(2ul)
                     .finish();
        sup->do_process();

        main();

        sup->do_process();
        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    std::int64_t files_scan_iteration_limit = 100;
    builder_ptr_t builder;
    r::pt::time_duration timeout = r::pt::millisec{10};
    r::intrusive_ptr_t<supervisor_t> sup;
    managed_hasher_t *hasher;
    cluster_ptr_t cluster;
    device_ptr_t my_device;
    bfs::path root_path;
    path_guard_t path_guard;
    target_ptr_t target;
    model::folder_ptr_t folder;
    model::folder_info_ptr_t folder_info;
    model::folder_info_ptr_t folder_info_peer;
    model::file_infos_map_t *files;
    model::file_infos_map_t *files_peer;
    model::device_ptr_t peer_device;
    fs::file_cache_ptr_t rw_cache;
};

void test_meta_changes() {
    struct F : fixture_t {
        void main() noexcept override {
            sys::error_code ec;

            SECTION("trivial") {
                SECTION("no files") {
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(folder_info->get_file_infos().size() == 0);
                }
#ifndef SYNCSPIRIT_WIN
                SECTION("just 1 subdir, which cannot be read") {
                    auto subdir = root_path / L"abc";
                    CHECK(bfs::create_directories(subdir / L"def", ec));
                    auto guard = test::path_guard_t(subdir);
                    bfs::permissions(subdir, bfs::perms::none);
                    bfs::permissions(subdir, bfs::perms::owner_read, ec);
                    if (!ec) {
                        builder->scan_start(folder->get_id()).apply(*sup);
                        REQUIRE(folder_info->get_file_infos().size() == 1);
                        auto fi = folder_info->get_file_infos().begin()->item;
                        CHECK(fi->is_dir());
                        CHECK(fi->get_name() == "abc");
                        bfs::permissions(subdir, bfs::perms::all);

                        auto &errs = sup->io_errors;
                        REQUIRE(errs.size() == 1);
                        REQUIRE(errs.at(0).path == (subdir / L"def"));
                        REQUIRE(errs.at(0).ec);
                    }
                }
#endif
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }

            auto modified = 1641828421;
            auto file_name = std::string_view("q.txt");
            proto::FileInfo pr_fi;
            proto::set_name(pr_fi, file_name);
            proto::set_type(pr_fi, proto::FileInfoType::FILE);
            proto::set_sequence(pr_fi, folder_info_peer->get_max_sequence() + 1);
            proto::set_size(pr_fi, 5);
            proto::set_block_size(pr_fi, 5);
            proto::set_modified_s(pr_fi, modified);

            auto &v = proto::get_version(pr_fi);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, peer_device->device_id().get_uint());
            proto::set_value(counter, 1);

            auto data_1 = as_owned_bytes("12345");
            auto data_1_hash = utils::sha256_digest(data_1).value();

            auto data_2 = as_owned_bytes("67890");
            auto data_2_hash = utils::sha256_digest(data_2).value();

            auto data_3 = as_owned_bytes("abcde");
            auto data_3_hash = utils::sha256_digest(data_3).value();

            auto bi = proto::BlockInfo();
            proto::set_hash(bi, data_1_hash);
            proto::set_size(bi, 5);

            auto bi_2 = proto::BlockInfo();
            proto::set_hash(bi_2, data_2_hash);
            proto::set_size(bi_2, 5);
            proto::set_offset(bi_2, 5);

            auto bi_3 = proto::BlockInfo();
            proto::set_hash(bi_3, data_3_hash);
            proto::set_size(bi_3, 5);
            proto::set_offset(bi_3, 10);

            auto b = block_info_t::create(bi).value();
            auto b2 = block_info_t::create(bi_2).value();
            auto b3 = block_info_t::create(bi_3).value();

            auto &blocks_map = cluster->get_blocks();
            blocks_map.put(b);
            SECTION("a file does not physically exist") {
                auto uuid = sup->sequencer->next_uuid();
                auto file_peer = file_info_t::create(uuid, pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                REQUIRE(folder_info_peer->add_strict(file_peer));
                builder->remote_copy(*file_peer).scan_start(folder->get_id()).apply(*sup);

                auto file = files->by_name(file_name);
                CHECK(files->size() == 1);
                CHECK(file->is_deleted());
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }
            SECTION("complete file exists") {
                auto uuid = sup->sequencer->next_uuid();
                auto file_peer = file_info_t::create(uuid, pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                REQUIRE(folder_info_peer->add_strict(file_peer));

                builder->remote_copy(*file_peer).apply(*sup);
                auto file = files->by_name(file_name);
                auto path = file->get_path();

                SECTION("meta is not changed") {
                    write_file(path, "12345");
                    bfs::last_write_time(path, from_unix(modified));
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(files->size() == 1);
                    CHECK(file->is_locally_available());
                }

                SECTION("meta is changed (modification)") {
                    write_file(path, "12345");
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(files->size() == 1);
                    auto new_file = files->by_name(file_name);
                    REQUIRE(new_file);
                    CHECK(file == new_file);
                    CHECK(new_file->is_locally_available());
                    CHECK(new_file->get_size() == 5);
                    REQUIRE(new_file->get_blocks().size() == 1);
                    CHECK(new_file->get_blocks()[0]->get_size() == 5);
                }

                SECTION("meta is changed (size)") {
                    write_file(path, "123456");
                    bfs::last_write_time(path, from_unix(modified));
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(files->size() == 1);
                    auto new_file = files->by_name(file_name);
                    REQUIRE(new_file);
                    CHECK(file == new_file);
                    CHECK(new_file->is_locally_available());
                    CHECK(new_file->get_size() == 6);
                    REQUIRE(new_file->get_blocks().size() == 1);
                    CHECK(new_file->get_blocks()[0]->get_size() == 6);
                }

                SECTION("meta is changed (content)") {
                    write_file(path, "67890");
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(files->size() == 1);
                    auto new_file = files->by_name(file_name);
                    REQUIRE(new_file);
                    CHECK(file == new_file);
                    CHECK(new_file->is_locally_available());
                    CHECK(new_file->get_size() == 5);
                    REQUIRE(new_file->get_blocks().size() == 1);
                    CHECK(new_file->get_blocks()[0]->get_size() == 5);
                }
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }

            SECTION("incomplete file exists") {
                proto::set_size(pr_fi, 10);
                proto::set_block_size(pr_fi, 5);

                auto uuid = sup->sequencer->next_uuid();
                auto file = file_info_t::create(uuid, pr_fi, folder_info_peer).value();
                file->assign_block(b, 0);
                file->assign_block(b2, 1);
                REQUIRE(folder_info_peer->add_strict(file));

                // auto file = files->by_name(pr_fi.name());
                auto filename = file->get_path().filename().wstring() + L".syncspirit-tmp";
                auto path = file->get_path().parent_path() / filename;
                auto content = "12345\0\0\0\0\0";
                write_file(path, std::string(content, 10));

                SECTION("outdated -> just remove") {
                    auto new_time = modified - 24 * 3600;
                    bfs::last_write_time(path, from_unix(new_time));
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(!file->is_locally_available());
                    CHECK(!file->is_locked());
                    CHECK(!bfs::exists(path));
                }

                SECTION("just 1st block is valid, tmp is kept") {
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(!file->is_locally_available());
                    CHECK(file->is_locally_available(0));
                    CHECK(!file->is_locally_available(1));
                    CHECK(bfs::exists(path));
                }

                SECTION("just 2nd block is valid, tmp is kept") {
                    write_file(path, "2234567890");
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(!file->is_locally_available());
                    CHECK(!file->is_locally_available(0));
                    CHECK(file->is_locally_available(1));
                    CHECK(bfs::exists(path));
                }

                SECTION("source is missing -> tmp is removed") {
                    folder_info_peer->get_file_infos().remove(file);
                    file->unlock();
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(!file->is_locally_available());
                    CHECK(!bfs::exists(path));
                }

                SECTION("corrupted content") {
                    SECTION("1st block") { write_file(path, "223456789z"); }
                    SECTION("2nd block") { write_file(path, "z234567899"); }
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(!file->is_locally_available(0));
                    CHECK(!file->is_locally_available(1));
                    CHECK(!bfs::exists(path));
                }

#ifndef SYNCSPIRIT_WIN
                SECTION("error on reading -> remove") {
                    bfs::permissions(path, bfs::perms::none);
                    if (!ec) {
                        builder->scan_start(folder->get_id()).apply(*sup);
                        CHECK(!file->is_locally_available());
                        CHECK(!file->is_locked());
                        CHECK(!bfs::exists(path));

                        auto &errs = sup->io_errors;
                        REQUIRE(errs.size() == 0);
                    }
                }
#endif
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }
            SECTION("local (previous) file exists") {
                proto::set_size(pr_fi, 15);
                proto::set_block_size(pr_fi, 5);

                proto::set_size(pr_fi, 5);
                auto uuid_1 = sup->sequencer->next_uuid();
                auto file_my = file_info_t::create(uuid_1, pr_fi, folder_info).value();
                file_my->assign_block(b, 0);
                file_my->lock();
                REQUIRE(folder_info->add_strict(file_my));

                proto::set_size(pr_fi, 15);
                proto::set_value(counter, 2);

                auto uuid_2 = sup->sequencer->next_uuid();
                auto file_peer = file_info_t::create(uuid_2, pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                file_peer->assign_block(b2, 1);
                file_peer->assign_block(b3, 2);
                REQUIRE(folder_info_peer->add_strict(file_peer));

                auto file = files->by_name(file_name);
                auto &path = file->get_path();
                auto filename = path.filename().wstring() + L".syncspirit-tmp";
                auto path_my = path;
                auto path_peer = path_my.parent_path() / filename;
                write_file(path_my, "12345");
                bfs::last_write_time(path_my, from_unix(modified));

                auto content = "1234567890\0\0\0\0\0";
                write_file(path_peer, std::string(content, 15));
                builder->scan_start(folder->get_id()).apply(*sup);

                CHECK(file_my->is_locally_available());
                CHECK(!file_peer->is_locally_available());
                CHECK(file_peer->is_locally_available(0));
                CHECK(file_peer->is_locally_available(1));
                CHECK(!file_peer->is_locally_available(2));
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }

            SECTION("local (previous) file changes") {
                proto::set_size(pr_fi, 15);
                proto::set_block_size(pr_fi, 5);
                auto uuid_1 = sup->sequencer->next_uuid();
                auto file_my = file_info_t::create(uuid_1, pr_fi, folder_info).value();
                file_my->assign_block(b, 0);
                file_my->assign_block(b, 1);
                file_my->assign_block(b, 2);
                file_my->lock();
                REQUIRE(folder_info->add_strict(file_my));

                proto::set_value(counter, 2);

                auto file = files->by_name(file_name);
                auto path_my = file->get_path();
                write_file(path_my, "12345");
                bfs::last_write_time(path_my, from_unix(modified));

                builder->scan_start(folder->get_id()).apply(*sup);

                CHECK(file_my->is_locally_available());
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }
        }
    };
    F().run();
}

void test_new_files() {
    struct F : fixture_t {
        void main() noexcept override {
            sys::error_code ec;
            auto &blocks = cluster->get_blocks();

#ifndef SYNCSPIRIT_WIN
            SECTION("new symlink") {
                auto file_path = root_path / "symlink";
                bfs::create_symlink(bfs::path("/some/where"), file_path, ec);
                REQUIRE(!ec);
                builder->scan_start(folder->get_id()).apply(*sup);

                auto file = files->by_name("symlink");
                REQUIRE(file);
                CHECK(file->is_locally_available());
                CHECK(!file->is_file());
                CHECK(file->is_link());
                CHECK(file->get_block_size() == 0);
                CHECK(file->get_size() == 0);
                CHECK(blocks.size() == 0);
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }
#endif

            SECTION("new dir") {
                auto dir_path = root_path / "some-dir";
                bfs::create_directories(dir_path);
                builder->scan_start(folder->get_id()).apply(*sup);

                auto file = files->by_name("some-dir");
                REQUIRE(file);
                CHECK(file->is_locally_available());
                CHECK(file->is_dir());
                CHECK(!file->is_link());
                CHECK(file->get_block_size() == 0);
                CHECK(file->get_size() == 0);
                CHECK(blocks.size() == 0);
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }

            SECTION("empty file") {
                CHECK(bfs::create_directories(root_path / "abc"));
                auto file_path = root_path / "abc" / "empty.file";
                write_file(file_path, "");
                builder->scan_start(folder->get_id()).apply(*sup);

                auto file = files->by_name("abc/empty.file");
                REQUIRE(file);
                CHECK(file->is_locally_available());
                CHECK(!file->is_link());
                CHECK(file->is_file());
                CHECK(file->get_block_size() == 0);
                CHECK(file->get_size() == 0);
                CHECK(blocks.size() == 0);
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }

            SECTION("non-empty file (1 block)") {
                auto file_path = root_path / "file.ext";
                write_file(file_path, "12345");
                builder->scan_start(folder->get_id()).apply(*sup);

                auto file = files->by_name("file.ext");
                REQUIRE(file);
                CHECK(file->is_locally_available());
                CHECK(!file->is_link());
                CHECK(file->is_file());
                CHECK(file->get_block_size() == 5);
                CHECK(file->get_size() == 5);
                CHECK(blocks.size() == 1);
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }

            SECTION("non-empty file (2 blocks)") {
                auto file_path = root_path / "file.ext";
                auto sz = int64_t{128 * 1024 * 2};
                std::string data(sz, 'x');
                write_file(file_path, data);
                builder->scan_start(folder->get_id()).apply(*sup);

                auto file = files->by_name("file.ext");
                REQUIRE(file);
                CHECK(file->is_locally_available());
                CHECK(!file->is_link());
                CHECK(file->is_file());
                CHECK(file->get_size() == sz);
                CHECK(file->get_blocks().size() == 2);
                CHECK(blocks.size() == 1);
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }

            SECTION("non-empty file (3 blocks)") {
                auto file_path = root_path / "file.ext";
                auto sz = std::int64_t{128 * 1024 * 3};
                std::string data(sz, 'x');
                write_file(file_path, data);
                builder->scan_start(folder->get_id()).apply(*sup);

                auto file = files->by_name("file.ext");
                REQUIRE(file);
                CHECK(file->is_locally_available());
                CHECK(!file->is_link());
                CHECK(file->is_file());
                CHECK(file->get_size() == sz);
                CHECK(file->get_blocks().size() == 3);
                CHECK(blocks.size() == 1);
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }

            SECTION("two files, different content") {
                auto file1_path = root_path / "file1.ext";
                write_file(file1_path, "12345");

                auto file2_path = root_path / "file2.ext";
                write_file(file2_path, "67890");
                builder->scan_start(folder->get_id()).apply(*sup);

                auto file1 = files->by_name("file1.ext");
                REQUIRE(file1);
                CHECK(file1->is_locally_available());
                CHECK(!file1->is_link());
                CHECK(file1->is_file());
                CHECK(file1->get_block_size() == 5);
                CHECK(file1->get_size() == 5);

                auto file2 = files->by_name("file2.ext");
                REQUIRE(file2);
                CHECK(file2->is_locally_available());
                CHECK(!file2->is_link());
                CHECK(file2->is_file());
                CHECK(file2->get_block_size() == 5);
                CHECK(file2->get_size() == 5);

                CHECK(blocks.size() == 2);
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }

            SECTION("two files, same content") {
                auto file1_path = root_path / "file1.ext";
                write_file(file1_path, "12345");

                auto file2_path = root_path / "file2.ext";
                write_file(file2_path, "12345");
                builder->scan_start(folder->get_id()).apply(*sup);

                auto file1 = files->by_name("file1.ext");
                REQUIRE(file1);
                CHECK(file1->is_locally_available());
                CHECK(!file1->is_link());
                CHECK(file1->is_file());
                CHECK(file1->get_block_size() == 5);
                CHECK(file1->get_size() == 5);

                auto file2 = files->by_name("file2.ext");
                REQUIRE(file2);
                CHECK(file2->is_locally_available());
                CHECK(!file2->is_link());
                CHECK(file2->is_file());
                CHECK(file2->get_block_size() == 5);
                CHECK(file2->get_size() == 5);

                CHECK(blocks.size() == 1);
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }
        }
    };
    F().run();
}

void test_remove_file() {
    struct F : fixture_t {
        void main() noexcept override {
            sys::error_code ec;
            auto &blocks = cluster->get_blocks();

            auto file_path = root_path / "file.ext";
            write_file(file_path, "12345");
            builder->scan_start(folder->get_id()).apply(*sup);
            REQUIRE(!folder->get_scan_finish().is_not_a_date_time());
            REQUIRE(!folder->is_scanning());

            auto file = files->by_name("file.ext");
            REQUIRE(file);
            REQUIRE(blocks.size() == 1);
            REQUIRE(file->get_version()->counters_size() == 1);
            auto counter = file->get_version()->get_best();

            auto prev_finish = folder->get_scan_finish();
            bfs::remove(file_path);

            builder->scan_start(folder->get_id()).apply(*sup);

            file = files->by_name("file.ext");
            CHECK(file->is_deleted() == 1);
            CHECK(file->is_local());
            CHECK(blocks.size() == 0);

            auto v = file->get_version();
            REQUIRE(v->counters_size() == 1);

            auto &c = v->get_best();
            CHECK(proto::get_id(c) == proto::get_id((counter)));
            CHECK(proto::get_value(c) > proto::get_value((counter)));
            REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            REQUIRE(folder->get_scan_finish() >= prev_finish);
        }
    };
    F().run();
};

void test_suspending() {
    struct F : fixture_t {
        F() { files_scan_iteration_limit = 1; }
        void main() noexcept override {
            sys::error_code ec;
            write_file(root_path / "f-1.ext", "123");
            write_file(root_path / "f-2.ext", "456");
            write_file(root_path / "f-3.ext", "789");
            builder->scan_start(folder->get_id()).suspend(*folder).apply(*sup);
            REQUIRE(!folder->get_scan_finish().is_not_a_date_time());
            REQUIRE(!folder->is_scanning());
        }
    };
    F().run();
};

void test_importing() {
    struct F : fixture_t {
        F() { files_scan_iteration_limit = 1; }
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            auto fi_peer = folder->get_folder_infos().by_device(*peer_device);
            SECTION("non-empty file") {
                auto path = root_path / "a.bin";
                write_file(path, "12345");
                auto status = bfs::status(path);
                auto permissions = static_cast<uint32_t>(status.permissions());

                auto pr_fi = proto::FileInfo();
                proto::set_name(pr_fi, path.filename().string());
                proto::set_type(pr_fi, proto::FileInfoType::FILE);
                proto::set_size(pr_fi, 5);
                proto::set_block_size(pr_fi, 5);
                proto::set_permissions(pr_fi, permissions);

                auto &v = proto::get_version(pr_fi);
                auto &counter = proto::add_counters(v);
                proto::set_id(counter, 1);
                proto::set_value(counter, 1);
                proto::set_sequence(pr_fi, fi_peer->get_max_sequence() + 1);

                auto data_1 = as_owned_bytes("12345");
                auto data_1_h = utils::sha256_digest(data_1).value();
                auto &b1 = proto::add_blocks(pr_fi);
                proto::set_hash(b1, data_1_h);
                proto::set_size(b1, data_1.size());

                builder->make_index(sha256, folder->get_id()).add(pr_fi, peer_device).finish().apply(*sup);

                builder->scan_start(folder->get_id()).apply(*sup);

                auto fi_my = folder->get_folder_infos().by_device(*my_device);
                auto &files_my = fi_my->get_file_infos();
                REQUIRE(files_my.size() == 1);
                auto file = files_my.by_name(path.filename().string());
                REQUIRE(file->get_version()->as_proto() == v);
            }
            SECTION("empty file") {
                auto path = root_path / "b.bin";
                write_file(path, "");
                auto status = bfs::status(path);
                auto permissions = static_cast<uint32_t>(status.permissions());

                auto pr_fi = proto::FileInfo();
                proto::set_name(pr_fi, path.filename().string());
                proto::set_type(pr_fi, proto::FileInfoType::FILE);
                proto::set_permissions(pr_fi, permissions);

                auto &v = proto::get_version(pr_fi);
                auto &counter = proto::add_counters(v);
                proto::set_id(counter, 1);
                proto::set_value(counter, 1);
                proto::set_sequence(pr_fi, fi_peer->get_max_sequence() + 1);

                builder->make_index(sha256, folder->get_id()).add(pr_fi, peer_device).finish().apply(*sup);

                builder->scan_start(folder->get_id()).apply(*sup);

                auto fi_my = folder->get_folder_infos().by_device(*my_device);
                auto &files_my = fi_my->get_file_infos();
                REQUIRE(files_my.size() == 1);
                auto file = files_my.by_name(path.filename().string());
                REQUIRE(file->get_version()->as_proto() == v);
            }
            SECTION("deleted") {
                SECTION("single deleted file") {
                    auto path = root_path / "c.bin";
                    auto pr_fi = proto::FileInfo();
                    proto::set_name(pr_fi, path.filename().string());
                    proto::set_type(pr_fi, proto::FileInfoType::DIRECTORY);
                    proto::set_deleted(pr_fi, true);

                    auto &v = proto::get_version(pr_fi);
                    auto &counter = proto::add_counters(v);
                    proto::set_id(counter, 1);
                    proto::set_value(counter, 1);
                    proto::set_sequence(pr_fi, fi_peer->get_max_sequence() + 1);

                    builder->make_index(sha256, folder->get_id()).add(pr_fi, peer_device).finish().apply(*sup);

                    builder->scan_start(folder->get_id()).apply(*sup);

                    auto fi_my = folder->get_folder_infos().by_device(*my_device);
                    auto &files_my = fi_my->get_file_infos();
                    REQUIRE(files_my.size() == 1);
                    auto file = files_my.by_name(path.filename().string());
                    REQUIRE(file->get_version()->as_proto() == v);
                    CHECK(!bfs::exists(path));
                }
                SECTION("deleted file inside deleted dir") {
                    auto path = root_path / "d" / "file.bin";
                    auto pr_dir = [&]() -> proto::FileInfo {
                        auto f = proto::FileInfo();
                        proto::set_name(f, "d");
                        proto::set_type(f, proto::FileInfoType::DIRECTORY);
                        proto::set_deleted(f, true);
                        auto &v = proto::get_version(f);
                        auto &counter = proto::add_counters(v);
                        proto::set_id(counter, 1);
                        proto::set_value(counter, 1);
                        proto::set_sequence(f, fi_peer->get_max_sequence() + 1);
                        return f;
                    }();
                    auto pr_file = [&]() -> proto::FileInfo {
                        auto f = proto::FileInfo();
                        proto::set_name(f, "d/file.bin");
                        proto::set_type(f, proto::FileInfoType::FILE);
                        proto::set_deleted(f, true);
                        auto &v = proto::get_version(f);
                        auto &counter = proto::add_counters(v);
                        proto::set_id(counter, 1);
                        proto::set_value(counter, 1);
                        proto::set_sequence(f, fi_peer->get_max_sequence() + 2);
                        return f;
                    }();

                    builder->make_index(sha256, folder->get_id())
                        .add(pr_dir, peer_device)
                        .add(pr_file, peer_device)
                        .finish()
                        .apply(*sup);

                    auto fi_my = folder->get_folder_infos().by_device(*my_device);
                    auto &files_my = fi_my->get_file_infos();
                    SECTION("prohibid creation of local deleted file") {
                        SECTION("dir is a file, locally") {
                            write_file(root_path / "d", "");
                            builder->scan_start(folder->get_id()).apply(*sup);
                            bfs::remove(root_path / "d");
                        }
#ifndef SYNCSPIRIT_WIN
                        SECTION("dir does exists, but it is non-readable") {
                            bfs::create_directory(root_path / "d");
                            bfs::permissions(root_path / "d", bfs::perms::none);
                            builder->scan_start(folder->get_id()).apply(*sup);
                            bfs::remove(root_path / "d");
                        }
#endif
                        CHECK(!files_my.by_name("d/file.bin"));
                    }
                    SECTION("allow creation of local deleted file") {
                        SECTION("dir does not exists") {
                            builder->scan_start(folder->get_id()).apply(*sup);
                            auto dir = files_my.by_name("d");
                            CHECK(dir);
                        }
                        SECTION("dir does exists & it is r/w") {
                            bfs::create_directory(root_path / "d");
                            builder->scan_start(folder->get_id()).apply(*sup);
                        }
                        REQUIRE(files_my.size() >= 1);
                        auto file = files_my.by_name("d/file.bin");
                        CHECK(file);
                        CHECK(!bfs::exists(path));
                    }
                }
                SECTION("prohibit creation of deleted record") {
                    auto path = root_path / "e" / "file.bin";
                    auto pr_dir = [&]() -> proto::FileInfo {
                        auto f = proto::FileInfo();
                        proto::set_name(f, "e");
                        proto::set_type(f, proto::FileInfoType::DIRECTORY);
                        proto::set_deleted(f, false);
                        auto &v = proto::get_version(f);
                        auto &counter = proto::add_counters(v);
                        proto::set_id(counter, 1);
                        proto::set_value(counter, 1);
                        proto::set_sequence(f, fi_peer->get_max_sequence() + 1);
                        return f;
                    }();
                    auto pr_file = [&]() -> proto::FileInfo {
                        auto f = proto::FileInfo();
                        proto::set_name(f, "e/file.bin");
                        proto::set_type(f, proto::FileInfoType::FILE);
                        proto::set_deleted(f, true);
                        auto &v = proto::get_version(f);
                        auto &counter = proto::add_counters(v);
                        proto::set_id(counter, 1);
                        proto::set_value(counter, 1);
                        proto::set_sequence(f, fi_peer->get_max_sequence() + 2);
                        return f;
                    }();

                    builder->make_index(sha256, folder->get_id())
                        .add(pr_dir, peer_device)
                        .add(pr_file, peer_device)
                        .finish()
                        .apply(*sup);

                    builder->scan_start(folder->get_id()).apply(*sup);

                    auto fi_my = folder->get_folder_infos().by_device(*my_device);
                    auto &files_my = fi_my->get_file_infos();
                    REQUIRE(files_my.size() == 0);
                }
            }
        }
    };
    F().run();
};

void test_races() {
    struct F : fixture_t {
        F() { files_scan_iteration_limit = 1; }
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            auto fi_peer = folder->get_folder_infos().by_device(*peer_device);
            auto fi_my = folder->get_folder_infos().by_device(*my_device);
            auto pr_fi = proto::FileInfo();
            proto::set_name(pr_fi, "a.bin");
            proto::set_type(pr_fi, proto::FileInfoType::FILE);
            proto::set_block_size(pr_fi, 5);

            auto &v = proto::get_version(pr_fi);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 1);
            proto::set_value(counter, 1);
            proto::set_sequence(pr_fi, fi_peer->get_max_sequence() + 1);

            auto data_1 = as_owned_bytes("12345");
            auto data_1_h = utils::sha256_digest(data_1).value();
            auto &b1 = proto::add_blocks(pr_fi);
            proto::set_hash(b1, data_1_h);
            proto::set_size(b1, data_1.size());

            SECTION("one-block-file") {
                proto::set_size(pr_fi, 5);
                builder->make_index(sha256, folder->get_id()).add(pr_fi, peer_device).finish().apply(*sup);

                auto file_peer = fi_peer->get_file_infos().by_name("a.bin");
                SECTION("non-finished/flushed new file") {
                    auto file_opt = fs::file_t::open_write(file_peer);
                    CHECK(file_opt);
                    // CHECK(file_opt.assume_error().message() == "zzz");
                    auto& file = file_opt.assume_value();
                    REQUIRE(bfs::exists(file.get_path()));
                    auto file_ptr = fs::file_ptr_t(new fs::file_t(std::move(file)));
                    rw_cache->put(file_ptr);
                    hasher->auto_reply = false;
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(fi_my->get_file_infos().size() == 0);
                    rw_cache->clear();
                    CHECK(hasher->digest_queue.size() == 0);
                    CHECK(hasher->validation_queue.size() == 0);
                }
                SECTION("non-finished/flushed existing file") {
                    auto path = root_path / "a.bin.syncspirit-tmp";
                    write_file(path, "12345");
                    builder->local_update(folder->get_id(), pr_fi).apply(*sup);
                    CHECK(fi_my->get_file_infos().size() == 1);

                    auto file_my = fi_my->get_file_infos().by_name("a.bin");
                    auto v1 = file_my->get_version()->as_proto();

                    file_peer->mark_local_available(0);
                    hasher->auto_reply = true;
                    builder->scan_start(folder->get_id()).apply(*sup);
                    bfs::remove(path);

                    auto v2 = file_my->get_version()->as_proto();
                    CHECK(v1 == v2);
                }
            }
            SECTION("multi blocks file") {
                proto::set_size(pr_fi, 10);
                auto data_2 = as_owned_bytes("67890");
                auto data_2_h = utils::sha256_digest(data_2).value();
                auto &b2 = proto::add_blocks(pr_fi);
                proto::set_hash(b2, data_2_h);
                proto::set_size(b2, data_2.size());
                proto::set_offset(b2, 5);

                builder->make_index(sha256, folder->get_id()).add(pr_fi, peer_device).finish().apply(*sup);

                auto file_peer = fi_peer->get_file_infos().by_name("a.bin");
                SECTION("non-finished/flushed new file") {
                    auto file = fs::file_t::open_write(file_peer).assume_value();
                    REQUIRE(bfs::exists(file.get_path()));
                    auto file_ptr = fs::file_ptr_t(new fs::file_t(std::move(file)));
                    rw_cache->put(file_ptr);
                    hasher->auto_reply = false;
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(fi_my->get_file_infos().size() == 0);
                    rw_cache->clear();
                    CHECK(hasher->digest_queue.size() == 0);
                    CHECK(hasher->validation_queue.size() == 0);
                    hasher->auto_reply = true;
                }
                SECTION("finished, but not flushed new file") {
                    auto path = root_path / "a.bin.syncspirit-tmp";
                    write_file(path, "1234567890");

                    builder->scan_start(folder->get_id()).apply(*sup);
                    auto file_my = fi_my->get_file_infos().by_name("a.bin");
                    CHECK(!file_my);
                    CHECK(file_peer->is_locally_available());
                }
                SECTION("non-finished/flushed existing file") {
                    auto path = root_path / "a.bin.syncspirit-tmp";
                    write_file(path, "1234500000");
                    builder->local_update(folder->get_id(), pr_fi).apply(*sup);
                    CHECK(fi_my->get_file_infos().size() == 1);

                    auto file_my = fi_my->get_file_infos().by_name("a.bin");
                    auto v1 = file_my->get_version()->as_proto();

                    file_peer->mark_local_available(0);
                    hasher->auto_reply = true;
                    builder->scan_start(folder->get_id()).apply(*sup);
                    bfs::remove(path);

                    auto v2 = file_my->get_version()->as_proto();
                    CHECK(v1 == v2);
                }
            }
        }
    };
    F().run();
}

int _init() {
    REGISTER_TEST_CASE(test_meta_changes, "test_meta_changes", "[fs]");
    REGISTER_TEST_CASE(test_new_files, "test_new_files", "[fs]");
    REGISTER_TEST_CASE(test_remove_file, "test_remove_file", "[fs]");
    REGISTER_TEST_CASE(test_suspending, "test_suspending", "[fs]");
    REGISTER_TEST_CASE(test_importing, "test_importing", "[fs]");
    REGISTER_TEST_CASE(test_races, "test_races", "[fs]");
    return 1;
}

static int v = _init();
