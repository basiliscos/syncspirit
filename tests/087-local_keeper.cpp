// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "fs/utils.h"
#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"
#include "managed_hasher.h"
#include "diff-builder.h"

#include "model/cluster.h"
#include "fs/messages.h"
#include "net/local_keeper.h"
#include "net/names.h"
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::fs;
using namespace syncspirit::hasher;

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

    fixture_t() noexcept : root_path{unique_path()}, path_guard{root_path} {
        test::init_logging();
        bfs::create_directory(root_path);
    }

    virtual std::uint32_t get_hash_limit() { return 1; }

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
                p.subscribe_actor(r::lambda<msg_t>([&](msg_t &req) {
                    sup->log->info("executing foreign task");
                    req.payload->exec(executor->hasher);
                }));
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

        target = sup->create_actor<net::local_keeper_t>()
                     .timeout(timeout)
                     .cluster(cluster)
                     .sequencer(make_sequencer(77))
                     .requested_hashes_limit(get_hash_limit())
                     .finish();
        sup->do_process();

        sup->send<syncspirit::model::payload::thread_ready_t>(sup->get_address(), cluster, std::this_thread::get_id());
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
};

void test_local_keeper() {
    struct F : fixture_t {
        std::uint32_t get_hash_limit() override { return 2; }

        void main() noexcept override {
            sys::error_code ec;
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
                auto &blocks = cluster->get_blocks();
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
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_local_keeper, "test_local_keeper", "[net]");
    return 1;
}

static int v = _init();
