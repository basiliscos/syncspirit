// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"
#include "diff-builder.h"

#include "model/cluster.h"
#include "model/diff/local/io_failure.h"
#include "hasher/hasher_proxy_actor.h"
#include "hasher/hasher_actor.h"
#include "fs/scan_actor.h"
#include "net/names.h"
#include "utils/error_code.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::hasher;

struct fixture_t {
    using target_ptr_t = r::intrusive_ptr_t<fs::scan_actor_t>;
    using builder_ptr_t = std::unique_ptr<diff_builder_t>;

    fixture_t() noexcept : root_path{bfs::unique_path()}, path_guard{root_path} {
        utils::set_default("trace");
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
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;

        auto folder_id = "1234-5678";

        sup->start();
        sup->do_process();
        builder = std::make_unique<diff_builder_t>(*cluster);
        builder->upsert_folder(folder_id, root_path.string())
            .apply(*sup)
            .share_folder(peer_id.get_sha256(), folder_id)
            .apply(*sup);

        folder = cluster->get_folders().by_id(folder_id);
        folder_info = folder->get_folder_infos().by_device(*my_device);
        files = &folder_info->get_file_infos();
        folder_info_peer = folder->get_folder_infos().by_device(*peer_device);
        files_peer = &folder_info_peer->get_file_infos();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        sup->create_actor<hasher_actor_t>().index(1).timeout(timeout).finish();
        auto proxy_addr = sup->create_actor<hasher::hasher_proxy_actor_t>()
                              .timeout(timeout)
                              .hasher_threads(1)
                              .name(net::names::hasher_proxy)
                              .finish()
                              ->get_address();

        sup->do_process();

        auto fs_config = config::fs_config_t{3600, 10, 1024 * 1024, 100};

        target = sup->create_actor<fs::scan_actor_t>()
                     .timeout(timeout)
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

    builder_ptr_t builder;
    r::pt::time_duration timeout = r::pt::millisec{10};
    r::intrusive_ptr_t<supervisor_t> sup;
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
                    auto subdir = root_path / "abc";
                    CHECK(bfs::create_directories(subdir / "def", ec));
                    auto guard = test::path_guard_t(subdir);
                    bfs::permissions(subdir, bfs::perms::no_perms);
                    bfs::permissions(subdir, bfs::perms::owner_read, ec);
                    if (!ec) {
                        builder->scan_start(folder->get_id()).apply(*sup);
                        REQUIRE(folder_info->get_file_infos().size() == 1);
                        auto fi = folder_info->get_file_infos().begin()->item;
                        CHECK(fi->is_dir());
                        CHECK(fi->get_name() == "abc");
                        bfs::permissions(subdir, bfs::perms::all_all);

                        auto &errs = sup->io_errors;
                        REQUIRE(errs.size() == 1);
                        REQUIRE(errs.at(0).path == (subdir / "def"));
                        REQUIRE(errs.at(0).ec);
                    }
                }
#endif
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            }

            proto::FileInfo pr_fi;
            std::int64_t modified = 1641828421;
            pr_fi.set_name("q.txt");
            pr_fi.set_modified_s(modified);
            pr_fi.set_block_size(5ul);
            pr_fi.set_size(5ul);
            pr_fi.set_sequence(folder_info_peer->get_max_sequence() + 1);

            auto version = pr_fi.mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1);
            counter->set_value(peer_device->device_id().get_uint());

            auto bi = proto::BlockInfo();
            bi.set_size(5);
            bi.set_weak_hash(12);
            bi.set_hash(utils::sha256_digest("12345").value());
            bi.set_offset(0);

            auto b = block_info_t::create(bi).value();
            auto &blocks_map = cluster->get_blocks();
            blocks_map.put(b);
            SECTION("a file does not physically exist") {
                auto uuid = sup->sequencer->next_uuid();
                auto file_peer = file_info_t::create(uuid, pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                REQUIRE(folder_info_peer->add_strict(file_peer));
                builder->remote_copy(*file_peer).scan_start(folder->get_id()).apply(*sup);

                auto file = files->by_name(pr_fi.name());
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
                auto file = files->by_name(pr_fi.name());
                auto path = file->get_path();

                SECTION("meta is not changed") {
                    write_file(path, "12345");
                    bfs::last_write_time(path, modified);
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(files->size() == 1);
                    CHECK(file->is_locally_available());
                }

                SECTION("meta is changed (modification)") {
                    write_file(path, "12345");
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(files->size() == 1);
                    auto new_file = files->by_name(pr_fi.name());
                    REQUIRE(new_file);
                    CHECK(file == new_file);
                    CHECK(new_file->is_locally_available());
                    CHECK(new_file->get_size() == 5);
                    REQUIRE(new_file->get_blocks().size() == 1);
                    CHECK(new_file->get_blocks()[0]->get_size() == 5);
                }

                SECTION("meta is changed (size)") {
                    write_file(path, "123456");
                    bfs::last_write_time(path, modified);
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(files->size() == 1);
                    auto new_file = files->by_name(pr_fi.name());
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
                    auto new_file = files->by_name(pr_fi.name());
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
                pr_fi.set_size(10ul);
                pr_fi.set_block_size(5ul);

                auto bi_2 = proto::BlockInfo();
                bi_2.set_size(5);
                bi_2.set_weak_hash(12);
                bi_2.set_hash(utils::sha256_digest("67890").value());
                bi_2.set_offset(5);
                auto b2 = block_info_t::create(bi_2).value();

                auto uuid = sup->sequencer->next_uuid();
                auto file = file_info_t::create(uuid, pr_fi, folder_info_peer).value();
                file->assign_block(b, 0);
                file->assign_block(b2, 1);
                REQUIRE(folder_info_peer->add_strict(file));

                // auto file = files->by_name(pr_fi.name());
                auto path = file->get_path().string() + ".syncspirit-tmp";
                auto content = "12345\0\0\0\0\0";
                write_file(path, std::string(content, 10));

                SECTION("outdated -> just remove") {
                    bfs::last_write_time(path, modified - 24 * 3600);
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
                    bfs::permissions(path, bfs::perms::no_perms);
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
                pr_fi.set_size(15ul);
                pr_fi.set_block_size(5ul);

                auto bi_2 = proto::BlockInfo();
                bi_2.set_size(5);
                bi_2.set_weak_hash(12);
                bi_2.set_hash(utils::sha256_digest("67890").value());
                bi_2.set_offset(5);
                auto b2 = block_info_t::create(bi_2).value();

                auto bi_3 = proto::BlockInfo();
                bi_3.set_size(5);
                bi_3.set_weak_hash(12);
                bi_3.set_hash(utils::sha256_digest("abcde").value());
                bi_3.set_offset(10);
                auto b3 = block_info_t::create(bi_3).value();

                pr_fi.set_size(5ul);
                auto uuid_1 = sup->sequencer->next_uuid();
                auto file_my = file_info_t::create(uuid_1, pr_fi, folder_info).value();
                file_my->assign_block(b, 0);
                file_my->lock();
                REQUIRE(folder_info->add_strict(file_my));

                pr_fi.set_size(15ul);
                counter->set_id(2);

                auto uuid_2 = sup->sequencer->next_uuid();
                auto file_peer = file_info_t::create(uuid_2, pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                file_peer->assign_block(b2, 1);
                file_peer->assign_block(b3, 2);
                REQUIRE(folder_info_peer->add_strict(file_peer));

                auto file = files->by_name(pr_fi.name());
                auto path_my = file->get_path().string();
                auto path_peer = file->get_path().string() + ".syncspirit-tmp";
                write_file(path_my, "12345");
                bfs::last_write_time(path_my, modified);

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
                pr_fi.set_size(15ul);
                pr_fi.set_block_size(5ul);
                auto uuid_1 = sup->sequencer->next_uuid();
                auto file_my = file_info_t::create(uuid_1, pr_fi, folder_info).value();
                file_my->assign_block(b, 0);
                file_my->assign_block(b, 1);
                file_my->assign_block(b, 2);
                file_my->lock();
                REQUIRE(folder_info->add_strict(file_my));

                pr_fi.set_size(15ul);
                counter->set_id(2);

                auto file = files->by_name(pr_fi.name());
                auto path_my = file->get_path().string();
                write_file(path_my, "12345");
                bfs::last_write_time(path_my, modified);

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
                auto sz = size_t{128 * 1024 * 2};
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
                auto sz = size_t{128 * 1024 * 3};
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
            CHECK(file->get_version()->counters_size() == 1);
            CHECK(file->get_version()->get_best().id() == counter.id());
            CHECK(file->get_version()->get_best().value() > counter.value());
            REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
            REQUIRE(folder->get_scan_finish() > prev_finish);
        }
    };
    F().run();
};

void test_synchronization() {
    struct F : fixture_t {
        void main() noexcept override {
            sys::error_code ec;
            auto &blocks = cluster->get_blocks();

            auto file_path = root_path / "file.ext";
            write_file(file_path, "12345");
            builder->scan_start(folder->get_id()).synchronization_start(folder->get_id()).apply(*sup);
            REQUIRE(!folder->get_scan_finish().is_not_a_date_time());
            REQUIRE(!folder->is_scanning());
            REQUIRE(files->size() == 0);
        }
    };
    F().run();
};

int _init() {
    REGISTER_TEST_CASE(test_meta_changes, "test_meta_changes", "[fs]");
    REGISTER_TEST_CASE(test_new_files, "test_new_files", "[fs]");
    REGISTER_TEST_CASE(test_remove_file, "test_remove_file", "[fs]");
    REGISTER_TEST_CASE(test_synchronization, "test_synchronization", "[fs]");
    return 1;
}

static int v = _init();
