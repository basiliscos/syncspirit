// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"
#include "diff-builder.h"

#include "model/cluster.h"
#include "model/diff/aggregate.h"
#include "model/diff/modify/clone_file.h"
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
    using error_msg_t = model::message::io_error_t;
    using error_msg_ptr_t = r::intrusive_ptr_t<error_msg_t>;
    using errors_container_t = std::vector<error_msg_ptr_t>;

    fixture_t() noexcept : root_path{bfs::unique_path()}, path_quard{root_path} {
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

        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<error_msg_t>([&](error_msg_t &msg) { errors.push_back(&msg); }));
            });
        };

        sup->start();
        sup->do_process();
        auto builder = diff_builder_t(*cluster);
        builder.create_folder(folder_id, root_path.string()).share_folder(peer_id.get_sha256(), folder_id).apply(*sup);

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

        auto fs_config = config::fs_config_t{3600, 10};

        target = sup->create_actor<fs::scan_actor_t>()
                     .timeout(timeout)
                     .cluster(cluster)
                     .hasher_proxy(proxy_addr)
                     .fs_config(fs_config)
                     .requested_hashes_limit(2ul)
                     .finish();

        main();

        sup->do_process();
        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    r::pt::time_duration timeout = r::pt::millisec{10};
    r::intrusive_ptr_t<supervisor_t> sup;
    cluster_ptr_t cluster;
    device_ptr_t my_device;
    bfs::path root_path;
    path_guard_t path_quard;
    target_ptr_t target;
    model::folder_ptr_t folder;
    model::folder_info_ptr_t folder_info;
    model::folder_info_ptr_t folder_info_peer;
    model::file_infos_map_t *files;
    model::file_infos_map_t *files_peer;
    errors_container_t errors;
    model::device_ptr_t peer_device;
};

void test_meta_changes() {
    struct F : fixture_t {
        void main() noexcept override {
            sys::error_code ec;

            SECTION("no files") {
                sup->do_process();
                CHECK(folder_info->get_file_infos().size() == 0);
            }
            SECTION("just 1 dir") {
                CHECK(bfs::create_directories(root_path / "abc"));
                sup->do_process();
                CHECK(folder_info->get_file_infos().size() == 0);
            }
            SECTION("just 1 subdir, which cannot be read") {
                auto subdir = root_path / "abc";
                CHECK(bfs::create_directories(subdir / "def", ec));
                auto guard = test::path_guard_t(subdir);
                bfs::permissions(subdir, bfs::perms::no_perms);
                bfs::permissions(subdir, bfs::perms::owner_read, ec);
                if (ec) {
                    sup->do_process();
                    CHECK(folder_info->get_file_infos().size() == 0);
                    bfs::permissions(subdir, bfs::perms::all_all);
                    REQUIRE(errors.size() == 1);
                    auto &errs = errors.at(0)->payload.errors;
                    REQUIRE(errs.size() == 1);
                    REQUIRE(errs.at(0).path == (subdir));
                    REQUIRE(errs.at(0).ec);
                }
            }

            proto::FileInfo pr_fi;
            std::int64_t modified = 1641828421;
            pr_fi.set_name("q.txt");
            pr_fi.set_modified_s(modified);
            pr_fi.set_block_size(5ul);
            pr_fi.set_size(5ul);

            auto version = pr_fi.mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1);
            counter->set_value(peer_device->as_uint());

            auto bi = proto::BlockInfo();
            bi.set_size(5);
            bi.set_weak_hash(12);
            bi.set_hash(utils::sha256_digest("12345").value());
            bi.set_offset(0);

            auto b = block_info_t::create(bi).value();

            SECTION("a file does not physically exists") {
                auto file_peer = file_info_t::create(cluster->next_uuid(), pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                folder_info_peer->add(file_peer, false);

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*file_peer));
                REQUIRE(diff->apply(*cluster));
                auto file = files->by_name(pr_fi.name());

                sup->do_process();
                CHECK(files->size() == 1);
                CHECK(!file->is_locally_available());
            }

            SECTION("complete file exists") {
                auto file_peer = file_info_t::create(cluster->next_uuid(), pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                folder_info_peer->add(file_peer, false);

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*file_peer));
                REQUIRE(diff->apply(*cluster));
                auto file = files->by_name(pr_fi.name());
                file->set_source(nullptr);
                auto path = file->get_path();

                SECTION("meta is not changed") {
                    write_file(path, "12345");
                    bfs::last_write_time(path, modified);
                    sup->do_process();
                    CHECK(files->size() == 1);
                    CHECK(file->is_locally_available());
                }
                SECTION("meta is changed (modification)") {
                    write_file(path, "12345");
                    sup->do_process();
                    CHECK(files->size() == 1);
                    CHECK(!file->is_locally_available());
                }
                SECTION("meta is changed (size)") {
                    write_file(path, "123456");
                    bfs::last_write_time(path, modified);
                    sup->do_process();
                    CHECK(files->size() == 1);
                    CHECK(!file->is_locally_available());
                }
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

                auto file_peer = file_info_t::create(cluster->next_uuid(), pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                file_peer->assign_block(b2, 1);
                folder_info_peer->add(file_peer, false);

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*file_peer));
                REQUIRE(diff->apply(*cluster));
                auto file = files->by_name(pr_fi.name());
                auto path = file->get_path().string() + ".syncspirit-tmp";
                file->lock(); // should be locked on db, as there is a source
                auto content = "12345\0\0\0\0\0";
                write_file(path, std::string(content, 10));

                SECTION("outdated -> just remove") {
                    bfs::last_write_time(path, modified - 24 * 3600);
                    sup->do_process();
                    CHECK(!file->is_locally_available());
                    CHECK(!file->is_locked());
                    CHECK(!bfs::exists(path));
                }

                SECTION("just 1st block is valid, tmp is kept") {
                    sup->do_process();
                    CHECK(!file->is_locally_available());
                    CHECK(!file->is_locally_available(0));
                    CHECK(!file->is_locally_available(1));
                    CHECK(!file->is_locked());
                    CHECK(!file_peer->is_locally_available());
                    CHECK(file_peer->is_locally_available(0));
                    CHECK(!file_peer->is_locally_available(1));
                    CHECK(bfs::exists(path));
                }

                SECTION("source is missing -> tmp is removed") {
                    file->set_source({});
                    file->unlock();
                    sup->do_process();
                    CHECK(!file->is_locally_available());
                    CHECK(!file->is_locked());
                    CHECK(!file_peer->is_locally_available());
                    CHECK(!file_peer->is_locally_available(0));
                    CHECK(!file_peer->is_locally_available(1));
                    CHECK(!bfs::exists(path));
                }

                SECTION("corrupted content") {
                    SECTION("1st block") { write_file(path, "2234567890"); }
                    SECTION("2nd block") { write_file(path, "1234567899"); }
                    SECTION("missing source file") { file->set_source(nullptr); }
                    sup->do_process();
                    CHECK(!file->is_locally_available(0));
                    CHECK(!file->is_locally_available(1));
                    CHECK(!file->is_locked());
                    CHECK(!file_peer->is_locally_available(0));
                    CHECK(!file_peer->is_locally_available(1));
                    CHECK(!bfs::exists(path));
                }

                SECTION("error on reading -> remove") {
                    bfs::permissions(path, bfs::perms::no_perms);
                    bfs::permissions(path, bfs::perms::owner_read, ec);
                    if (ec) {
                        sup->do_process();
                        CHECK(!file->is_locally_available());
                        CHECK(file->is_locked());
                        CHECK(!bfs::exists(path));

                        REQUIRE(errors.size() == 1);
                        auto &errs = errors.at(0)->payload.errors;
                        REQUIRE(errs.size() == 1);
                        CHECK(errs.at(0).path == path);
                        CHECK(errs.at(0).ec);
                    }
                }
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
                auto file_my = file_info_t::create(cluster->next_uuid(), pr_fi, folder_info).value();
                file_my->assign_block(b, 0);
                file_my->lock();
                folder_info->add(file_my, false);

                pr_fi.set_size(15ul);
                counter->set_id(2);

                auto file_peer = file_info_t::create(cluster->next_uuid(), pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                file_peer->assign_block(b2, 1);
                file_peer->assign_block(b3, 2);
                folder_info_peer->add(file_peer, false);

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*file_peer));
                REQUIRE(diff->apply(*cluster));
                auto file = files->by_name(pr_fi.name());
                auto path_my = file->get_path().string();
                auto path_peer = file->get_path().string() + ".syncspirit-tmp";
                write_file(path_my, "12345");
                bfs::last_write_time(path_my, modified);

                auto content = "1234567890\0\0\0\0\0";
                write_file(path_peer, std::string(content, 15));
                sup->do_process();

                CHECK(file_my->is_locally_available());
                CHECK(file_my->get_source() == file_peer);
                CHECK(!file_peer->is_locally_available());
                CHECK(file_peer->is_locally_available(0));
                CHECK(file_peer->is_locally_available(1));
                CHECK(!file_peer->is_locally_available(2));
            }
        }
    };
    F().run();
}

void test_new_files() {
    struct F : fixture_t {
        void main() noexcept override {
            sys::error_code ec;
            auto& blocks = cluster->get_blocks();

            SECTION("new symlink") {
                auto file_path = root_path / "symlink";
                bfs::create_symlink(bfs::path("/some/where"), file_path, ec);
                REQUIRE(!ec);
                sup->do_process();

                auto file = files->by_name("symlink");
                REQUIRE(file);
                CHECK(!file->is_file());
                CHECK(file->is_link());
                CHECK(file->get_block_size() == 0);
                CHECK(file->get_size() == 0);
                CHECK(blocks.size() == 0);
            }

            SECTION("empty file") {
                CHECK(bfs::create_directories(root_path / "abc"));
                auto file_path = root_path / "abc" / "empty.file";
                write_file(file_path, "");
                sup->do_process();

                auto file = files->by_name("abc/empty.file");
                REQUIRE(file);
                CHECK(!file->is_link());
                CHECK(file->is_file());
                CHECK(file->get_block_size() == 0);
                CHECK(file->get_size() == 0);
                CHECK(blocks.size() == 0);
            }

            SECTION("non-empty file") {
                CHECK(bfs::create_directories(root_path / "abc"));
                auto file_path = root_path / "file.ext";
                write_file(file_path, "12345");
                sup->do_process();

                auto file = files->by_name("file.ext");
                REQUIRE(file);
                CHECK(!file->is_link());
                CHECK(file->is_file());
                CHECK(file->get_block_size() == 5);
                CHECK(file->get_size() == 5);
                CHECK(blocks.size() == 1);
            }

            SECTION("two files, diffrent content") {
                CHECK(bfs::create_directories(root_path / "abc"));
                auto file1_path = root_path / "file1.ext";
                write_file(file1_path, "12345");

                auto file2_path = root_path / "file2.ext";
                write_file(file2_path, "67890");
                sup->do_process();

                auto file1 = files->by_name("file1.ext");
                REQUIRE(file1);
                CHECK(!file1->is_link());
                CHECK(file1->is_file());
                CHECK(file1->get_block_size() == 5);
                CHECK(file1->get_size() == 5);

                auto file2 = files->by_name("file2.ext");
                REQUIRE(file2);
                CHECK(!file2->is_link());
                CHECK(file2->is_file());
                CHECK(file2->get_block_size() == 5);
                CHECK(file2->get_size() == 5);

                CHECK(blocks.size() == 2);
            }

            SECTION("two files, same content") {
                CHECK(bfs::create_directories(root_path / "abc"));
                auto file1_path = root_path / "file1.ext";
                write_file(file1_path, "12345");

                auto file2_path = root_path / "file2.ext";
                write_file(file2_path, "12345");
                sup->do_process();

                auto file1 = files->by_name("file1.ext");
                REQUIRE(file1);
                CHECK(!file1->is_link());
                CHECK(file1->is_file());
                CHECK(file1->get_block_size() == 5);
                CHECK(file1->get_size() == 5);

                auto file2 = files->by_name("file2.ext");
                REQUIRE(file2);
                CHECK(!file2->is_link());
                CHECK(file2->is_file());
                CHECK(file2->get_block_size() == 5);
                CHECK(file2->get_size() == 5);

                CHECK(blocks.size() == 1);
            }

        }
    };
    F().run();
}

int _init() {
    REGISTER_TEST_CASE(test_meta_changes, "test_meta_changes", "[fs]");
    REGISTER_TEST_CASE(test_new_files, "test_new_files", "[fs]");
    return 1;
}

static int v = _init();
