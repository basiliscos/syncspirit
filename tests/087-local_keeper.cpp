// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "access.h"
#include "diff-builder.h"
#include "fs/fs_slave.h"
#include "fs/messages.h"
#include "fs/utils.h"
#include "managed_hasher.h"
#include "model/cluster.h"
#include "net/local_keeper.h"
#include "net/names.h"
#include "test-utils.h"
#include "test_supervisor.h"

#include <boost/nowide/convert.hpp>

#ifndef SYNCSPIRIT_WIN
#include <sys/types.h>
#include <sys/stat.h>
#endif

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::fs;
using namespace syncspirit::hasher;

using task_processor_t = std::function<void(fs::fs_slave_t *)>;

struct executor_t : r::actor_base_t {
    using parent_t = r::actor_base_t;
    using parent_t::parent_t;

    // clang-format off
    using plugins_list_t = std::tuple<
        r::plugin::address_maker_plugin_t,
        r::plugin::lifetime_plugin_t,
        r::plugin::init_shutdown_plugin_t,
        r::plugin::link_server_plugin_t,
        r::plugin::link_client_plugin_t,
        hasher::hasher_plugin_t,
        r::plugin::resources_plugin_t,
        r::plugin::starter_plugin_t
    >;
    // clang-format on

    void configure(r::plugin::plugin_base_t &plugin) noexcept {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<hasher::hasher_plugin_t>([&](auto &p) {
            hasher = &p;
            p.configure_hashers(1);
        });
    }

    hasher_plugin_t *hasher;
};

using executor_ptr_t = r::intrusive_ptr_t<executor_t>;

struct fixture_t {
    using target_ptr_t = r::intrusive_ptr_t<net::local_keeper_t>;
    using builder_ptr_t = std::unique_ptr<diff_builder_t>;

    fixture_t(bool auto_launch_ = true) noexcept
        : root_path{unique_path()}, path_guard{root_path}, auto_launch{auto_launch_} {
        test::init_logging();
        bfs::create_directory(root_path);
    }

    virtual std::uint32_t get_hash_limit() { return 1; }

    virtual void execute(fs::message::foreign_executor_t &req) noexcept {
        sup->log->info("executing foreign task");
        auto slave = static_cast<fs::fs_slave_t *>(req.payload.get());
        slave->ec = {};
        req.payload->exec(executor->hasher);
    }

    void run() noexcept {
        auto my_hash = "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD";
        auto my_id = device_id_t::from_string(my_hash).value();
        my_device = device_t::create(my_id, "my-device").value();

        auto peer_hash = "VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ";
        auto peer_id = device_id_t::from_string(peer_hash).value();
        peer_device = device_t::create(peer_id, "peer-device").value();

        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().make_presentation(true).timeout(timeout).create_registry().finish();
        sup->cluster = cluster;
        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::registry_plugin_t>(
                [&](auto &p) { p.register_name(net::names::fs_actor, sup->get_address()); });
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                using msg_t = fs::message::foreign_executor_t;
                p.subscribe_actor(r::lambda<msg_t>([&](msg_t &req) { execute(req); }));
            });
        };

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

        executor = sup->create_actor<executor_t>().timeout(timeout).finish();
        hasher = sup->create_actor<managed_hasher_t>().index(1).auto_reply(true).timeout(timeout).finish().get();
        sup->do_process();

        auto fs_config = config::fs_config_t{3600, 10, 1024 * 1024, files_scan_iteration_limit};
        rw_cache.reset(new fs::file_cache_t(5));

        if (auto_launch) {
            launch_target();
        }

        main();

        sup->do_process();
        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    void launch_target() {
        target = sup->create_actor<net::local_keeper_t>()
                     .timeout(timeout)
                     .cluster(cluster)
                     .sequencer(make_sequencer(77))
                     .requested_hashes_limit(get_hash_limit())
                     .finish();
        sup->do_process();

        sup->send<syncspirit::model::payload::thread_ready_t>(sup->get_address(), cluster, std::this_thread::get_id());
        sup->do_process();
    }

    virtual void main() noexcept {}

    std::int64_t files_scan_iteration_limit = 100;
    builder_ptr_t builder;
    r::pt::time_duration timeout = r::pt::millisec{10};
    r::intrusive_ptr_t<supervisor_t> sup;
    executor_ptr_t executor;
    managed_hasher_t *hasher;
    cluster_ptr_t cluster;
    device_ptr_t my_device;
    bfs::path root_path;
    test::path_guard_t path_guard;
    target_ptr_t target;
    model::folder_ptr_t folder;
    model::folder_info_ptr_t folder_info;
    model::folder_info_ptr_t folder_info_peer;
    model::file_infos_map_t *files;
    model::file_infos_map_t *files_peer;
    model::device_ptr_t peer_device;
    fs::file_cache_ptr_t rw_cache;
    bool auto_launch;
};

void test_simple() {
    struct F : fixture_t {
        std::uint32_t get_hash_limit() override { return 2; }

        void main() noexcept override {
            sys::error_code ec;
            auto &blocks = cluster->get_blocks();
            auto my_short_id = my_device->device_id().get_uint();
            SECTION("root folder errors") {
                auto dir_path = root_path / "some-dir";
                folder->set_path(dir_path);
                builder->scan_start(folder->get_id()).apply(*sup);
                REQUIRE(folder->is_suspended());
                REQUIRE(folder->get_suspend_reason().message() != "");
                SECTION("scan on created again") {
                    bfs::create_directories(dir_path);
                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(!folder->is_suspended());
                    REQUIRE(!folder->get_suspend_reason());
                }
                CHECK(!folder->is_scanning());
                CHECK(folder->get_scan_finish() >= folder->get_scan_start());
            }
            SECTION("emtpy root dir") {
                auto dir_path = root_path / "some-dir";
                bfs::create_directories(dir_path);
                folder->set_path(dir_path);
                builder->scan_start(folder->get_id()).apply(*sup);
                CHECK(!folder->is_scanning());
                CHECK(folder->get_scan_finish() >= folder->get_scan_start());
            }
            SECTION("new items") {
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
                    CHECK(cluster->get_blocks().size() == 0);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                }
                SECTION("new dir inside a new ir") {
                    auto name_1 = bfs::path(L"п1");
                    auto name_2 = name_1 / bfs::path(L"п2");
                    auto dir_path = root_path / name_2;
                    bfs::create_directories(dir_path);
                    builder->scan_start(folder->get_id()).apply(*sup);

                    REQUIRE(cluster->get_blocks().size() == 0);

                    auto f_1 = files->by_name(boost::nowide::narrow(name_1.generic_wstring()));
                    REQUIRE(f_1);
                    CHECK(f_1->is_locally_available());
                    CHECK(f_1->is_dir());

                    auto f_2 = files->by_name(boost::nowide::narrow(name_2.generic_wstring()));
                    REQUIRE(f_2);
                    CHECK(f_2->is_locally_available());
                    CHECK(f_2->is_dir());
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
                SECTION("small non-emtpy file") {
                    CHECK(bfs::create_directories(root_path / L"папка"));
                    auto part_path = bfs::path(L"папка") / L"файл.bin";
                    auto file_path = root_path / part_path;
                    write_file(file_path, "12345");
                    builder->scan_start(folder->get_id()).apply(*sup);

                    auto file = files->by_name(boost::nowide::narrow(part_path.generic_wstring()));
                    REQUIRE(file);
                    CHECK(file->is_locally_available());
                    CHECK(!file->is_link());
                    CHECK(file->is_file());
                    CHECK(file->get_block_size() == 5);
                    CHECK(file->get_size() == 5);
                    CHECK(blocks.size() == 1);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                }
                SECTION("2 blocks file") {
                    CHECK(bfs::create_directories(root_path / L"папка"));
                    auto part_path = bfs::path(L"папка") / L"файл.bin";
                    auto file_path = root_path / part_path;
                    auto block_sz = fs::block_sizes[0];
                    auto b1 = std::string(block_sz, '0');
                    auto b2 = std::string(block_sz, '1');
                    write_file(file_path, b1 + b2);
                    builder->scan_start(folder->get_id()).apply(*sup);

                    auto file = files->by_name(boost::nowide::narrow(part_path.generic_wstring()));
                    REQUIRE(file);
                    CHECK(file->is_locally_available());
                    CHECK(!file->is_link());
                    CHECK(file->is_file());
                    CHECK(file->get_block_size() == block_sz);
                    CHECK(file->get_size() == block_sz * 2);
                    CHECK(blocks.size() == 2);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                }
                SECTION("2 blocks + 1 byte file") {
                    CHECK(bfs::create_directories(root_path / L"папка"));
                    auto part_path = bfs::path(L"папка") / L"файл.bin";
                    auto file_path = root_path / part_path;
                    auto block_sz = fs::block_sizes[0];
                    auto b1 = std::string(block_sz, '0');
                    auto b2 = std::string(block_sz, '1');
                    auto b3 = std::string(1, '2');
                    write_file(file_path, b1 + b2 + b3);
                    builder->scan_start(folder->get_id()).apply(*sup);

                    auto file = files->by_name(boost::nowide::narrow(part_path.generic_wstring()));
                    REQUIRE(file);
                    CHECK(file->is_locally_available());
                    CHECK(!file->is_link());
                    CHECK(file->is_file());
                    CHECK(file->get_block_size() == block_sz);
                    CHECK(file->get_size() == block_sz * 2 + 1);
                    CHECK(blocks.size() == 3);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                }
            }
            SECTION("unchanged items") {
                auto pr_file = proto::FileInfo{};
                auto file_name = bfs::path(L"неизменное.bin");
                proto::set_name(pr_file, file_name.string());
                proto::set_sequence(pr_file, 4);
                auto &v = proto::get_version(pr_file);
                auto &counter = proto::add_counters(v);
                proto::set_id(counter, my_short_id);
                proto::set_value(counter, 1);

                SECTION("dir") {
                    auto dir = root_path / file_name;
                    bfs::create_directories(dir);
                    auto modified = to_unix(bfs::last_write_time(dir));
                    auto status = bfs::status(dir);
                    auto perms = static_cast<uint32_t>(status.permissions());

                    proto::set_type(pr_file, proto::FileInfoType::DIRECTORY);
                    proto::set_permissions(pr_file, perms);
                    proto::set_modified_s(pr_file, modified);

                    builder->local_update(folder->get_id(), pr_file).apply(*sup);
                    REQUIRE(files->size() == 1);
                    auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    file_1->mark_local(false);
                    CHECK(!file_1->is_local());

                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                    REQUIRE(files->size() == 1);

                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    CHECK(file_1.get() == file_2.get());
                    CHECK(file_1->is_local());
                }
                SECTION("regular file") {
                    auto path = root_path / file_name;
                    auto data = std::string("12345");
                    write_file(path, data);
                    auto modified = to_unix(bfs::last_write_time(path));
                    auto status = bfs::status(path);
                    auto perms = static_cast<uint32_t>(status.permissions());

                    proto::set_permissions(pr_file, perms);
                    proto::set_modified_s(pr_file, modified);
                    proto::set_size(pr_file, 5);
                    proto::set_block_size(pr_file, 5);
                    auto b = proto::BlockInfo();
                    proto::set_size(b, 5);
                    proto::set_hash(b, utils::sha256_digest(as_bytes(data)).value());
                    proto::add_blocks(pr_file, b);

                    builder->local_update(folder->get_id(), pr_file).apply(*sup);
                    REQUIRE(files->size() == 1);
                    REQUIRE(blocks.size() == 1);
                    auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    file_1->mark_local(false);
                    CHECK(!file_1->is_local());

                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                    REQUIRE(files->size() == 1);

                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    CHECK(file_1.get() == file_2.get());
                    CHECK(file_1->is_local());
                    REQUIRE(blocks.size() == 1);
                }

#ifndef SYNCSPIRIT_WIN
                SECTION("symlink") {
                    auto file_path = root_path / file_name;
                    auto target = bfs::path(L"/куда-то/where");
                    bfs::create_symlink(target, file_path);
                    auto status = bfs::symlink_status(file_path);
                    auto perms = static_cast<uint32_t>(status.permissions());

                    proto::set_symlink_target(pr_file, boost::nowide::narrow(target.wstring()));
                    proto::set_type(pr_file, proto::FileInfoType::SYMLINK);
                    proto::set_permissions(pr_file, perms);

                    builder->local_update(folder->get_id(), pr_file).apply(*sup);
                    REQUIRE(files->size() == 1);
                    auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    file_1->mark_local(false);
                    REQUIRE(file_1->is_link());
                    REQUIRE(file_1->get_link_target() == boost::nowide::narrow(target.wstring()));
                    CHECK(!file_1->is_local());

                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                    REQUIRE(files->size() == 1);

                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    CHECK(file_1.get() == file_2.get());
                    CHECK(file_1->is_local());
                }
#endif
            }
        }
    };
    F().run();
}

void test_deleted() {
    struct F : fixture_t {
        void main() noexcept override {
            sys::error_code ec;
            auto &blocks = cluster->get_blocks();
            auto my_short_id = my_device->device_id().get_uint();

            SECTION("sigle items") {
                auto pr_file = proto::FileInfo{};
                auto file_name = bfs::path(L"неизменное.bin");
                proto::set_deleted(pr_file, true);
                proto::set_name(pr_file, file_name.string());
                proto::set_sequence(pr_file, 4);
                auto &v = proto::get_version(pr_file);
                auto &counter = proto::add_counters(v);
                proto::set_id(counter, my_short_id);
                proto::set_value(counter, 1);

                SECTION("regular file") { builder->local_update(folder->get_id(), pr_file).apply(*sup); }
#ifndef SYNCSPIRIT_WIN
                SECTION("symlink") {
                    proto::set_symlink_target(pr_file, "does/not/matter");
                    proto::set_type(pr_file, proto::FileInfoType::SYMLINK);
                    builder->local_update(folder->get_id(), pr_file).apply(*sup);
                }
#endif
                SECTION("regular dir") {
                    proto::set_type(pr_file, proto::FileInfoType::DIRECTORY);
                    builder->local_update(folder->get_id(), pr_file).apply(*sup);
                }

                REQUIRE(files->size() == 1);

                auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                file_1->mark_local(false);
                CHECK(!file_1->is_local());
                REQUIRE(file_1->is_deleted());

                builder->scan_start(folder->get_id()).apply(*sup);
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                REQUIRE(files->size() == 1);

                auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                CHECK(file_1.get() == file_2.get());
                CHECK(file_1->is_local());
                REQUIRE(file_1->is_deleted());
            }
            SECTION("deleted hierarchy of dirs") {
                auto pr_file = proto::FileInfo{};
                proto::set_type(pr_file, proto::FileInfoType::DIRECTORY);
                proto::set_deleted(pr_file, true);
                proto::set_sequence(pr_file, 4);
                auto &v = proto::get_version(pr_file);
                auto &counter = proto::add_counters(v);
                proto::set_id(counter, my_short_id);
                proto::set_value(counter, 1);
                auto file_name = bfs::path(L"имя");
                for (auto &name : {"a", "a/bb", "a/cc", "a/bb/ddd"}) {
                    proto::set_name(pr_file, name);
                    builder->local_update(folder->get_id(), pr_file);
                }
                builder->apply(*sup);
                REQUIRE(files->size() == 4);
                for (auto f : *files) {
                    f->mark_local(false);
                }

                builder->scan_start(folder->get_id()).apply(*sup);
                REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                CHECK(files->size() == 4);
                for (auto f : *files) {
                    CHECK(f->is_local());
                    CHECK(f->is_deleted());
                }
            }
        }
    };
    F().run();
}

void test_changed() {
    struct F : fixture_t {
        void main() noexcept override {
            sys::error_code ec;
            auto &blocks = cluster->get_blocks();
            auto my_short_id = my_device->device_id().get_uint();
            SECTION("single items") {
                SECTION("change all content of a small file") {
                    auto data_1 = as_owned_bytes("12345");
                    auto data_2 = as_owned_bytes("67890");
                    auto data_x = as_owned_bytes("5432109876");

                    auto hash_1 = utils::sha256_digest(data_1).value();
                    auto hash_2 = utils::sha256_digest(data_2).value();
                    auto hash_4 = utils::sha256_digest(data_x).value();

                    auto pr_file = proto::FileInfo{};
                    auto file_name = bfs::path(L"файлик.bin");
                    auto file_path = root_path / file_name;

                    proto::set_name(pr_file, file_name.string());
                    proto::set_sequence(pr_file, 4);
                    auto &v = proto::get_version(pr_file);
                    auto &counter = proto::add_counters(v);
                    proto::set_id(counter, my_short_id);
                    proto::set_value(counter, 1);

                    auto b_1 = proto::BlockInfo();
                    proto::set_hash(b_1, hash_1);
                    proto::set_size(b_1, data_1.size());

                    auto b_2 = proto::BlockInfo();
                    proto::set_hash(b_2, hash_2);
                    proto::set_offset(b_2, data_1.size());
                    proto::set_size(b_2, data_2.size());

                    proto::add_blocks(pr_file, b_1);
                    proto::add_blocks(pr_file, b_2);
                    proto::set_size(pr_file, data_1.size() + data_2.size());

                    builder->local_update(folder->get_id(), pr_file).apply(*sup);
                    REQUIRE(files->size() == 1);
                    REQUIRE(blocks.size() == 2);

                    write_file(file_path, "5432109876");

                    auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    file_1->mark_local(false);
                    auto seq_1 = file_1->get_sequence();

                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());

                    CHECK(files->size() == 1);
                    CHECK(blocks.size() == 1);
                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    CHECK(file_2->is_local());
                    REQUIRE(file_2->iterate_blocks().get_total() == 1);
                    CHECK(file_2->iterate_blocks().next()->get_hash() == hash_4);
                    auto seq_2 = file_2->get_sequence();
                    CHECK(seq_2 > seq_1);
                }
                SECTION("meta changed") {
                    auto data_1_str = std::string("12345");
                    auto data_1 = as_owned_bytes(data_1_str);
                    auto hash_1 = utils::sha256_digest(data_1).value();

                    auto pr_file = proto::FileInfo{};
                    auto file_name = bfs::path(L"файлик.bin");
                    auto file_path = root_path / file_name;

                    proto::set_name(pr_file, file_name.string());
                    proto::set_sequence(pr_file, 4);
                    auto &v = proto::get_version(pr_file);
                    auto &counter = proto::add_counters(v);
                    proto::set_id(counter, my_short_id);
                    proto::set_value(counter, 1);

                    auto b_1 = proto::BlockInfo();
                    proto::set_hash(b_1, hash_1);
                    proto::set_size(b_1, data_1.size());

                    proto::add_blocks(pr_file, b_1);
                    proto::set_size(pr_file, data_1.size());

                    write_file(file_path, data_1_str);

                    auto status = bfs::status(file_path);
                    auto modified = to_unix(bfs::last_write_time(file_path));
                    auto perms = static_cast<uint32_t>(status.permissions());

                    SECTION("modificaiton time changed") {
                        proto::set_permissions(pr_file, perms);
                        proto::set_modified_s(pr_file, modified - 1);
                    }
                    SECTION("permissions changed") {
                        proto::set_permissions(pr_file, perms - 1);
                        proto::set_modified_s(pr_file, modified);
                    }

                    builder->local_update(folder->get_id(), pr_file).apply(*sup);
                    REQUIRE(files->size() == 1);
                    REQUIRE(blocks.size() == 1);

                    auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    file_1->mark_local(false);
                    auto seq_1 = file_1->get_sequence();

                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());

                    CHECK(files->size() == 1);
                    CHECK(blocks.size() == 1);
                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    CHECK(file_2->is_local());
                    REQUIRE(file_2->iterate_blocks().get_total() == 1);
                    CHECK(file_2->iterate_blocks().next()->get_hash() == hash_1);
                    auto seq_2 = file_2->get_sequence();
                    CHECK(seq_2 > seq_1);
                }
                SECTION("append new lbock") {
                    auto block_sz = fs::block_sizes[0];
                    auto b1 = std::string(block_sz, '0');
                    auto b2 = std::string(block_sz, '1');
                    auto b3 = std::string(block_sz, '2');

                    auto data_1 = as_owned_bytes(b1);
                    auto data_2 = as_owned_bytes(b2);
                    auto data_3 = as_owned_bytes(b3);

                    auto hash_1 = utils::sha256_digest(data_1).value();
                    auto hash_2 = utils::sha256_digest(data_2).value();
                    auto hash_3 = utils::sha256_digest(data_3).value();

                    auto pr_file = proto::FileInfo{};
                    auto file_name = bfs::path(L"файлик.bin");
                    auto file_path = root_path / file_name;

                    proto::set_name(pr_file, file_name.string());
                    proto::set_sequence(pr_file, 4);
                    auto &v = proto::get_version(pr_file);
                    auto &counter = proto::add_counters(v);
                    proto::set_id(counter, my_short_id);
                    proto::set_value(counter, 1);

                    auto b_1 = proto::BlockInfo();
                    proto::set_hash(b_1, hash_1);
                    proto::set_size(b_1, data_1.size());

                    auto b_2 = proto::BlockInfo();
                    proto::set_hash(b_2, hash_2);
                    proto::set_offset(b_2, data_1.size());
                    proto::set_size(b_2, data_2.size());

                    proto::add_blocks(pr_file, b_1);
                    proto::add_blocks(pr_file, b_2);
                    proto::set_size(pr_file, data_1.size() + data_2.size());

                    builder->local_update(folder->get_id(), pr_file).apply(*sup);
                    REQUIRE(files->size() == 1);
                    REQUIRE(blocks.size() == 2);

                    write_file(file_path, b1 + b2 + b3);

                    auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    file_1->mark_local(false);
                    auto seq_1 = file_1->get_sequence();

                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());

                    CHECK(files->size() == 1);
                    CHECK(blocks.size() == 3);
                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    CHECK(file_2->is_local());
                    REQUIRE(file_2->iterate_blocks().get_total() == 3);
                    REQUIRE(file_2->iterate_blocks(2).next()->get_hash() == hash_3);
                    REQUIRE(file_2->get_size() == block_sz * 3);
                    auto seq_2 = file_2->get_sequence();
                    CHECK(seq_2 > seq_1);
                }
            }
        }
    };
    F().run();
}

void test_type_change() {
    struct F : fixture_t {
        void main() noexcept override {
            auto my_short_id = my_device->device_id().get_uint();
            auto pr_file = proto::FileInfo{};
            auto file_name = bfs::path(L"файлик.bin");
            proto::set_name(pr_file, file_name.string());
            proto::set_sequence(pr_file, 4);
            auto &v = proto::get_version(pr_file);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, my_short_id);
            proto::set_value(counter, 1);
            auto file_path = root_path / file_name;

            SECTION("has been dir") {
                proto::set_type(pr_file, proto::FileInfoType::DIRECTORY);
                builder->local_update(folder->get_id(), pr_file).apply(*sup);
                auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                REQUIRE(file_1->is_dir());
                auto seq_1 = file_1->get_sequence();

                SECTION(" -> regular file") {
                    write_file(file_path, "");
                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    REQUIRE(file_2->is_file());
                    auto seq_2 = file_2->get_sequence();
                    CHECK(seq_2 > seq_1);
                }
#ifndef SYNCSPIRIT_WIN
                SECTION(" -> symlink") {
                    bfs::create_symlink(bfs::path("/some/where"), file_path);
                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    REQUIRE(file_2->is_link());
                    auto seq_2 = file_2->get_sequence();
                    CHECK(seq_2 > seq_1);
                }
#endif
            }
            SECTION("has been regular file") {
                proto::set_type(pr_file, proto::FileInfoType::FILE);
                builder->local_update(folder->get_id(), pr_file).apply(*sup);
                auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                REQUIRE(file_1->is_file());
                auto seq_1 = file_1->get_sequence();
                SECTION(" -> dir") {
                    bfs::create_directories(file_path);
                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    REQUIRE(file_2->is_dir());
                    auto seq_2 = file_2->get_sequence();
                    CHECK(seq_2 > seq_1);
                }
#ifndef SYNCSPIRIT_WIN
                SECTION(" -> symlink") {
                    bfs::create_symlink(bfs::path("/some/where"), file_path);
                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    REQUIRE(file_2->is_link());
                    auto seq_2 = file_2->get_sequence();
                    CHECK(seq_2 > seq_1);
                }
#endif
            }
#ifndef SYNCSPIRIT_WIN
            SECTION("has been symlink") {
                proto::set_type(pr_file, proto::FileInfoType::SYMLINK);
                builder->local_update(folder->get_id(), pr_file).apply(*sup);
                auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                REQUIRE(file_1->is_link());
                auto seq_1 = file_1->get_sequence();
                SECTION(" -> dir") {
                    bfs::create_directories(file_path);
                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    REQUIRE(file_2->is_dir());
                    auto seq_2 = file_2->get_sequence();
                    CHECK(seq_2 > seq_1);
                }
                SECTION(" -> regular file") {
                    write_file(file_path, "");
                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    REQUIRE(file_2->is_file());
                    auto seq_2 = file_2->get_sequence();
                    CHECK(seq_2 > seq_1);
                }
            }
#endif
        }
    };
    F().run();
}

void test_scan_errors() {
    struct F : fixture_t {
        F() {
            processor = [&](fs::fs_slave_t *slave) {
                slave->ec = {};
                sup->log->info("executing foreign task");
                slave->exec(executor->hasher);
            };
        }

        void execute(fs::message::foreign_executor_t &req) noexcept override {
            if (exec_pool) {
                sup->log->info("processing foreign task (left = {})", exec_pool);
                auto slave = static_cast<fs::fs_slave_t *>(req.payload.get());
                processor(slave);
                --exec_pool;
            } else {
                sup->log->info("ignoring foreign task execution");
            }
        }

        void main() noexcept override {
            SECTION("root dir errors") {
                SECTION("missing root dir") {
                    auto dir_path = root_path / "some-dir";
                    folder->set_path(dir_path);
                }
                SECTION("no permissings to read outer dir") {
                    auto dir_path = root_path / "some-dir";
                    folder->set_path(dir_path);
                    bfs::permissions(root_path, bfs::perms::none);
                }
                builder->scan_start(folder->get_id()).apply(*sup);
                REQUIRE(folder->is_suspended());
                REQUIRE(folder->get_suspend_reason().message() != "");
                CHECK(!folder->is_scanning());
                CHECK(folder->get_scan_finish() >= folder->get_scan_start());
                bfs::permissions(root_path, bfs::perms::all);
            }
#ifndef SYNCSPIRIT_WIN
            SECTION("non-root errors (non-win32)") {
                auto dir_path = root_path / "d1" / "d2";
                auto d1_path = dir_path.parent_path();
                bfs::create_directories(dir_path);

                auto status = bfs::status(d1_path);
                auto perms = status.permissions();
                bfs::permissions(d1_path, perms, bfs::perm_options::remove);

                builder->scan_start(folder->get_id()).apply(*sup);
                REQUIRE(!folder->is_suspended());
                CHECK(!folder->is_scanning());
                CHECK(folder->get_scan_finish() >= folder->get_scan_start());
                CHECK(files->size() == 1);
                CHECK((*files->begin())->get_name()->get_own_name() == "d1");
                bfs::permissions(d1_path, perms, bfs::perm_options::add);
            }
            SECTION("non-sync'able entity (named fifo file)") {
                auto fifo_path = root_path / "fifo";
                auto fifo_str = boost::nowide::narrow(fifo_path.wstring());
                REQUIRE(mknod(fifo_str.c_str(), S_IFIFO | 0666, 0) == 0);
                builder->scan_start(folder->get_id()).apply(*sup);
                CHECK(!folder->is_scanning());
                CHECK(folder->get_scan_finish() >= folder->get_scan_start());
                CHECK(files->size() == 0);
            }
#endif
            SECTION("generic task error") {
                exec_pool = 2;
                auto dir_path = root_path / "d1" / "d2";
                auto d1_path = dir_path.parent_path();
                bfs::create_directories(dir_path);

                builder->scan_start(folder->get_id()).apply(*sup);
                CHECK(!folder->is_scanning());
                CHECK(folder->get_scan_finish() >= folder->get_scan_start());
                CHECK(files->size() == 1);
            }

            SECTION("scan dir errors") {
                int mocked = 0;
                auto generator_type = GENERATE(0, 1);
                auto do_mock = [&](bfs::path dir_path) {
                    processor = [&, dir_path = dir_path](fs::fs_slave_t *slave) {
                        slave->ec = {};
                        bool do_exec = true;
                        if (!slave->tasks_in.empty()) {
                            auto task = &slave->tasks_in.front();
                            auto scan_task = std::get_if<fs::task::scan_dir_t>(task);
                            if (scan_task) {
                                if (scan_task->path == dir_path) {
                                    ++mocked;
                                    do_exec = false;
                                    auto ec = r::make_error_code(r::error_code_t::cancelled);
                                    sup->log->info("mocking result {}", generator_type);
                                    if (generator_type == 0) {
                                        scan_task->ec = {};
                                        auto info = fs::task::scan_dir_t::child_info_t();
                                        info.path = dir_path / "xx";
                                        info.ec = ec;
                                        scan_task->child_infos.emplace_back(std::move(info));
                                    } else {
                                        scan_task->ec = ec;
                                    }
                                    slave->tasks_out.emplace_back(std::move(*task));
                                    slave->tasks_in.pop_front();
                                }
                            }
                        }
                        if (do_exec) {
                            sup->log->info("executing foreign task");
                            slave->exec(executor->hasher);
                        }
                    };
                };

                auto my_short_id = my_device->device_id().get_uint();
                auto dir_path = root_path / "d1";
                auto d1_path = dir_path.parent_path();
                bfs::create_directories(dir_path);

                auto pr_dir = proto::FileInfo{};
                proto::set_name(pr_dir, "d1");
                proto::set_sequence(pr_dir, 4);
                auto &v = proto::get_version(pr_dir);
                auto &counter = proto::add_counters(v);
                proto::set_id(counter, my_short_id);
                proto::set_value(counter, 1);
                builder->local_update(folder->get_id(), pr_dir).apply(*sup);
                CHECK(files->size() == 1);
                auto dir_file = *files->begin();

                SECTION("new file") {
                    do_mock(dir_path);
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(files->size() == 1);
                }
                SECTION("existing file") {
                    auto pr_file = proto::FileInfo{};
                    proto::set_name(pr_file, "d1/xx");
                    proto::set_sequence(pr_file, 5);
                    auto &v = proto::get_version(pr_file);
                    auto &counter = proto::add_counters(v);
                    proto::set_id(counter, my_short_id);
                    proto::set_value(counter, 1);
                    builder->local_update(folder->get_id(), pr_file).apply(*sup);

                    do_mock(dir_path);
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(files->size() == 2);

                    auto file = files->by_name("d1/xx");
                    CHECK(file->is_locally_available());
                }

                CHECK(dir_file->is_locally_available());
                CHECK(mocked == 1);
                CHECK(!folder->is_scanning());
                CHECK(folder->get_scan_finish() >= folder->get_scan_start());
            }
        }

        std::uint32_t exec_pool = 5;
        task_processor_t processor;
    };
    F().run();
}

void test_read_errors() {
    struct F : fixture_t {
        F() : fixture_t{false} {
            processor = [&](fs::fs_slave_t *slave) {
                slave->ec = {};
                sup->log->info("executing foreign task");
                slave->exec(executor->hasher);
            };
        }

        std::uint32_t get_hash_limit() override { return hash_limit; }

        void execute(fs::message::foreign_executor_t &req) noexcept override {
            if (exec_pool) {
                sup->log->info("processing foreign task (left = {})", exec_pool);
                auto slave = static_cast<fs::fs_slave_t *>(req.payload.get());
                processor(slave);
                --exec_pool;
            } else {
                sup->log->info("ignoring foreign task execution");
            }
        }

        void main() noexcept override {
#ifndef SYNCSPIRIT_WIN
            SECTION("small unknown file") {
                launch_target();
                auto file_path = root_path / "file.bin";
                write_file(file_path, "12345");
                bfs::permissions(file_path, bfs::perms::all, bfs::perm_options::remove);
                builder->scan_start(folder->get_id()).apply(*sup);
                CHECK(!folder->is_scanning());
                CHECK(folder->get_scan_finish() >= folder->get_scan_start());
                CHECK(files->size() == 0);
            }
#endif
            SECTION("large unknown file") {
                auto file_path = root_path / "file.bin";
                auto block_sz = fs::block_sizes[0];
                auto count = std::uint32_t{5};
                auto b = std::string(block_sz * count, 'x');
                write_file(file_path, b);

                auto do_mock = [&](std::int32_t block_index) {
                    processor = [&, count, block_index](fs::fs_slave_t *slave) {
                        slave->ec = {};
                        bool do_exec = true;
                        if (!slave->tasks_in.empty()) {
                            auto task = &slave->tasks_in.front();
                            auto iterator = std::get_if<fs::task::segment_iterator_t>(task);
                            if (iterator) {
                                if (iterator->block_index == block_index) {
                                    do_exec = false;
                                    auto ec = r::make_error_code(r::error_code_t::cancelled);
                                    iterator->ec = ec;
                                    sup->log->info("mocking result {} block", block_index);
                                    slave->tasks_out.emplace_back(std::move(*task));
                                    slave->tasks_in.pop_front();
                                }
                            }
                        }
                        if (do_exec) {
                            sup->log->info("executing foreign task");
                            slave->exec(executor->hasher);
                        }
                    };
                };

                using pair_t = std::tuple<std::int32_t, std::int32_t>;
                // clang-format off
                auto pair = GENERATE(table<std::int32_t, std::int32_t>({
                    pair_t{0, 1},
                    pair_t{1, 1},
                    pair_t{2, 1},
                    pair_t{3, 1},
                    pair_t{4, 1},
                    pair_t{2, 2},
                }));
                // clang-format on

                std::int32_t block_index;
                std::tie(block_index, hash_limit) = pair;

                do_mock(block_index);
                launch_target();

                INFO("block index = " << block_index << ", hash limit = " << hash_limit);
                builder->scan_start(folder->get_id()).apply(*sup);
                CHECK(!folder->is_scanning());
                CHECK(folder->get_scan_finish() >= folder->get_scan_start());
                CHECK(files->size() == 0);
                CHECK(hasher->digested_blocks <= block_index + hash_limit);
            }
        }
        std::uint32_t exec_pool = 10;
        task_processor_t processor;
        std::uint32_t hash_limit = 1;
    };
    F().run();
};

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_simple, "test_simple", "[net]");
    REGISTER_TEST_CASE(test_deleted, "test_deleted", "[net]");
    REGISTER_TEST_CASE(test_changed, "test_changed", "[net]");
    REGISTER_TEST_CASE(test_type_change, "test_type_change", "[net]");
    REGISTER_TEST_CASE(test_scan_errors, "test_scan_errors", "[net]");
    REGISTER_TEST_CASE(test_read_errors, "test_read_errors", "[net]");
    return 1;
}

static int v = _init();
