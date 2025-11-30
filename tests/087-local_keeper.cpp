// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "access.h"
#include "diff-builder.h"
#include "fs/fs_slave.h"
#include "fs/messages.h"
#include "fs/utils.h"
#include "managed_hasher.h"
#include "model/cluster.h"
#include "model/diff/advance/local_update.h"
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

struct fixture_t;

struct my_supervisort_t : supervisor_t {
    using parent_t = supervisor_t;
    using parent_t::parent_t;

    fixture_t *fixture = nullptr;

    void on_model_update(model::message::model_update_t &) noexcept override;
    outcome::result<void> operator()(const model::diff::advance::local_update_t &, void *) noexcept override;
};

struct fixture_t {
    using target_ptr_t = r::intrusive_ptr_t<net::local_keeper_t>;
    using builder_ptr_t = std::unique_ptr<diff_builder_t>;

    fixture_t(bool auto_launch_ = true) noexcept
        : root_path{unique_path()}, path_guard{root_path}, auto_launch{auto_launch_} {
        test::init_logging();
        bfs::create_directory(root_path);
    }

    virtual std::uint32_t get_hash_limit() { return 1; }

    virtual std::int64_t get_iterations_limit() { return 100; }

    virtual void execute(fs::message::foreign_executor_t &req) noexcept {
        sup->log->info("executing foreign task");
        auto slave = static_cast<fs::fs_slave_t *>(req.payload.get());
        slave->ec = {};
        req.payload->exec(executor->hasher);
    }

    virtual void launch_hasher() noexcept {
        hasher = sup->create_actor<managed_hasher_t>().index(1).auto_reply(true).timeout(timeout).finish().get();
    }

    virtual void on_diff(const model::diff::advance::local_update_t &) noexcept {}
    virtual void on_model_update(model::message::model_update_t &) noexcept {}

    void run() noexcept {
        sequencer = make_sequencer(1234);
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
        sup = ctx.create_supervisor<my_supervisort_t>()
                  .make_presentation(true)
                  .timeout(timeout)
                  .create_registry()
                  .finish();
        sup->cluster = cluster;
        static_cast<my_supervisort_t *>(sup.get())->fixture = this;
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
        launch_hasher();
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
                     .sequencer(sequencer)
                     .concurrent_hashes(get_hash_limit())
                     .files_scan_iteration_limit(get_iterations_limit())
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
    model::sequencer_ptr_t sequencer;
    fs::file_cache_ptr_t rw_cache;
    bool auto_launch;
};

auto my_supervisort_t::operator()(const model::diff::advance::local_update_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    fixture->on_diff(diff);
    return parent_t::operator()(diff, custom);
}

void my_supervisort_t::on_model_update(model::message::model_update_t &diff) noexcept {
    fixture->on_model_update(diff);
    return parent_t::on_model_update(diff);
}

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
#ifndef SYNCSPIRIT_WIN
                    CHECK(!file->has_no_permissions());
                    CHECK(file->get_permissions() == static_cast<uint32_t>(bfs::status(file_path).permissions()));
#else
                    CHECK(file->has_no_permissions());
                    CHECK(file->get_permissions() == 0666);
#endif
                }
#ifndef SYNCSPIRIT_WIN
                SECTION("new symlink") {
                    auto file_path = root_path / "symlink";
                    auto target = std::string_view("/some/where");
                    bfs::create_symlink(bfs::path(target), file_path, ec);
                    REQUIRE(!ec);
                    builder->scan_start(folder->get_id()).apply(*sup);

                    auto file = files->by_name("symlink");
                    REQUIRE(file);
                    CHECK(file->is_locally_available());
                    CHECK(!file->is_file());
                    CHECK(file->is_link());
                    CHECK(file->get_block_size() == 0);
                    CHECK(file->get_size() == 0);
                    CHECK(file->get_link_target() == target);
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

void test_no_changes() {
    struct F : fixture_t {
        std::uint32_t get_hash_limit() override { return 2; }

        void main() noexcept override {
            sys::error_code ec;
            auto &blocks = cluster->get_blocks();
            auto my_short_id = my_device->device_id().get_uint();
            auto folder_id = folder->get_id();

            auto v = proto::Vector();
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 1);
            proto::set_value(counter, 1);

            SECTION("single item") {
                auto pr_file = proto::FileInfo{};
                auto file_name = bfs::path(L"неизменное.bin");
                proto::set_name(pr_file, file_name.string());
                proto::set_sequence(pr_file, 4);
                proto::set_version(pr_file, v);

                SECTION("dir") {
                    auto dir = root_path / file_name;
                    bfs::create_directories(dir);
                    auto modified = to_unix(bfs::last_write_time(dir));
                    auto status = bfs::status(dir);
                    auto perms = static_cast<uint32_t>(status.permissions());

                    proto::set_type(pr_file, proto::FileInfoType::DIRECTORY);
                    proto::set_permissions(pr_file, perms);
                    proto::set_modified_s(pr_file, modified);

                    builder->local_update(folder_id, pr_file).apply(*sup);
                    REQUIRE(files->size() == 1);
                    auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    file_1->mark_local(false);
                    CHECK(!file_1->is_local());

                    builder->scan_start(folder_id).apply(*sup);
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

                    builder->local_update(folder_id, pr_file).apply(*sup);
                    REQUIRE(files->size() == 1);
                    REQUIRE(blocks.size() == 1);
                    auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    file_1->mark_local(false);
                    CHECK(!file_1->is_local());

                    builder->scan_start(folder_id).apply(*sup);
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

                    builder->local_update(folder_id, pr_file).apply(*sup);
                    REQUIRE(files->size() == 1);
                    auto file_1 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    file_1->mark_local(false);
                    REQUIRE(file_1->is_link());
                    REQUIRE(file_1->get_link_target() == boost::nowide::narrow(target.wstring()));
                    CHECK(!file_1->is_local());

                    builder->scan_start(folder_id).apply(*sup);
                    REQUIRE(folder->get_scan_finish() >= folder->get_scan_start());
                    REQUIRE(files->size() == 1);

                    auto file_2 = files->by_name(boost::nowide::narrow(file_name.wstring()));
                    CHECK(file_1.get() == file_2.get());
                    CHECK(file_1->is_local());
                }
#endif
            }
            SECTION("dir & file") {
                auto file_name_1 = bfs::path(L"b-dir");
                auto dir = root_path / file_name_1;
                bfs::create_directories(dir);

                auto modified_1 = to_unix(bfs::last_write_time(dir));
                auto perms_1 = static_cast<uint32_t>(bfs::status(dir).permissions());

                auto pr_file_1 = proto::FileInfo{};
                proto::set_name(pr_file_1, file_name_1.string());
                proto::set_version(pr_file_1, v);
                proto::set_type(pr_file_1, proto::FileInfoType::DIRECTORY);
                proto::set_permissions(pr_file_1, perms_1);
                proto::set_modified_s(pr_file_1, modified_1);

                auto file_name_2 = bfs::path(L"a-file");
                auto file = root_path / file_name_2;
                write_file(file, "");

                auto modified_2 = to_unix(bfs::last_write_time(file));
                auto perms_2 = static_cast<uint32_t>(bfs::status(file).permissions());

                auto pr_file_2 = proto::FileInfo{};
                proto::set_name(pr_file_2, file_name_2.string());
                proto::set_version(pr_file_2, v);
                proto::set_type(pr_file_2, proto::FileInfoType::FILE);
                proto::set_permissions(pr_file_2, perms_2);
                proto::set_modified_s(pr_file_2, modified_2);

                builder->local_update(folder_id, pr_file_1).local_update(folder_id, pr_file_2).apply(*sup);
                REQUIRE(files->size() == 2);

                auto sequence = folder_info->get_max_sequence();
                builder->scan_start(folder_id).apply(*sup);
                CHECK(folder_info->get_max_sequence() == sequence);
            }
        }
    };
    F().run();
}

void test_deleted() {
    struct F : fixture_t {
        using paths_t = std::vector<std::string>;

        void on_diff(const model::diff::advance::local_update_t &diff) noexcept override {
            auto file = folder_info->get_file_infos().by_uuid(diff.uuid);
            auto name = file->get_name()->get_full_name();
            paths.emplace_back(std::string(name));
        }

        void main() noexcept override {
            sys::error_code ec;
            auto &blocks = cluster->get_blocks();
            auto my_short_id = my_device->device_id().get_uint();

            auto v = proto::Vector();
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, my_short_id);
            proto::set_value(counter, 1);

            SECTION("sigle items") {
                auto pr_file = proto::FileInfo{};
                auto file_name = bfs::path(L"неизменное.bin");
                proto::set_name(pr_file, file_name.string());
                proto::set_sequence(pr_file, 4);
                proto::set_version(pr_file, v);

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
                proto::set_deleted(pr_file, false);
                proto::set_sequence(pr_file, 4);
                proto::set_version(pr_file, v);

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
            SECTION("order of deletion (1)") {
                auto file_type = GENERATE(0, 1);

                auto pr_file = proto::FileInfo{};
                proto::set_type(pr_file, static_cast<proto::FileInfoType>(file_type));
                proto::set_deleted(pr_file, false);
                proto::set_sequence(pr_file, 4);
                proto::set_version(pr_file, v);

                for (auto &name : {"a", "b", "c", "d"}) {
                    proto::set_name(pr_file, name);
                    builder->local_update(folder->get_id(), pr_file);
                }
                builder->apply(*sup);
                REQUIRE(files->size() == 4);
                for (auto f : *files) {
                    f->mark_local(false);
                }
                paths = {};
                builder->scan_start(folder->get_id()).apply(*sup);

                auto expected = paths_t{
                    "a",
                    "b",
                    "c",
                    "d",
                };
                CHECK(paths == expected);

                builder->scan_start(folder->get_id()).apply(*sup);
                CHECK(paths == expected);
            }
            SECTION("order of deletion (2)") {
                auto file_type = GENERATE(0, 1);

                auto pr_file = proto::FileInfo{};
                proto::set_type(pr_file, proto::FileInfoType::DIRECTORY);
                proto::set_deleted(pr_file, false);
                proto::set_sequence(pr_file, 4);
                proto::set_version(pr_file, v);

                for (auto &name : {"a", "a/1", "a/2", "a/3", "b", "b/1", "b/2", "b/3", "a/2/xx", "a/2/yy"}) {
                    proto::set_name(pr_file, name);
                    builder->local_update(folder->get_id(), pr_file);
                }
                builder->apply(*sup);
                REQUIRE(files->size() == 10);
                for (auto f : *files) {
                    f->mark_local(false);
                }
                paths = {};
                builder->scan_start(folder->get_id()).apply(*sup);

                // clang-format off
                auto expected = paths_t{
                    "a",
                    "a/1",
                    "a/2",
                    "a/2/xx",
                    "a/2/yy",
                    "a/3",
                    "b", "b/1", "b/2",  "b/3",
                };
                // clang-format on
                CHECK(paths == expected);

                builder->scan_start(folder->get_id()).apply(*sup);
                CHECK(paths == expected);
            }
        }

        paths_t paths;
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
                exec_pool = 1;
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

void test_leaks() {
    struct F : fixture_t {
        void launch_hasher() noexcept override {
            hasher = sup->create_actor<managed_hasher_t>().index(1).auto_reply(false).timeout(timeout).finish().get();
        }
        void main() noexcept override {
            write_file(root_path / "file-1.bin", "12345");
            write_file(root_path / "file-2.bin", "12345");
            builder->scan_start(folder->get_id()).apply(*sup);
            CHECK(folder->is_scanning());

            target->do_shutdown();
            sup->do_process();
            CHECK(files->size() == 0);

            hasher->process_requests();
            sup->do_process();
        }
    };
    F().run();
}

void test_hashing_fail() {
    struct F : fixture_t {
        std::uint32_t get_hash_limit() override { return 2; }
        void launch_hasher() noexcept override {
            hasher = sup->create_actor<managed_hasher_t>().index(1).subscribe(false).timeout(timeout).finish().get();
        }
        void main() noexcept override {
            auto block_sz = fs::block_sizes[0];
            auto b = std::string(block_sz * 5, 'x');
            write_file(root_path / "file.bin", b);
            builder->scan_start(folder->get_id()).apply(*sup);

            sup->do_process();
            CHECK(!folder->get_scan_start().is_special());
            CHECK(!folder->get_scan_finish().is_special());
            CHECK(folder->get_scan_finish() >= folder->get_scan_start());
            CHECK(files->size() == 0);
            CHECK(target->get_shutdown_reason());
        }
    };
    F().run();
}

void test_incomplete() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            auto block_sz = fs::block_sizes[0];
            auto path = root_path / "файл.syncspirit-tmp";

            auto pr_file = proto::FileInfo{};
            auto file_name = boost::nowide::narrow(L"файл");
            proto::set_name(pr_file, file_name);
            proto::set_sequence(pr_file, 4);
            auto &v = proto::get_version(pr_file);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 55);
            proto::set_value(counter, 1);
            proto::set_modified_s(pr_file, 12345);

            auto data_1 = as_owned_bytes("12345");
            auto data_2 = as_owned_bytes("67890");
            auto hash_1 = utils::sha256_digest(data_1).value();
            auto hash_2 = utils::sha256_digest(data_2).value();
            auto b_1 = proto::BlockInfo();
            proto::set_hash(b_1, hash_1);
            proto::set_size(b_1, data_1.size());
            auto b_2 = proto::BlockInfo();
            proto::set_hash(b_2, hash_2);
            proto::set_offset(b_2, data_1.size());
            proto::set_size(b_2, data_2.size());

            SECTION("no in model => remove") {
                write_file(path, "");
                builder->scan_start(folder->get_id()).apply(*sup);

                CHECK(files->size() == 0);
                CHECK(!bfs::exists(path));
            }
            SECTION("exists only in my model => remove") {
                write_file(path, "");
                builder->local_update(folder->get_id(), pr_file)
                    .apply(*sup)
                    .then()
                    .scan_start(folder->get_id())
                    .apply(*sup);

                CHECK(files->size() == 1);
                CHECK(!bfs::exists(path));
            }
            SECTION("found in peer model, size mismatch => remove") {
                proto::add_blocks(pr_file, b_1);
                proto::set_size(pr_file, data_1.size());
                builder->make_index(sha256, folder->get_id()).add(pr_file, peer_device).finish().apply(*sup);

                builder->scan_start(folder->get_id()).apply(*sup);

                write_file(path, "");
                builder->scan_start(folder->get_id()).apply(*sup);

                CHECK(files->size() == 0);
                CHECK(!bfs::exists(path));
            }
            SECTION("2 blocks peer file") {
                proto::add_blocks(pr_file, b_1);
                proto::add_blocks(pr_file, b_2);
                proto::set_size(pr_file, data_1.size() + data_2.size());

                auto model_path = root_path / file_name;
                SECTION("all blocks match => rename & add into model") {
                    write_file(path, "1234567890");
                    auto status = bfs::status(path);
                    auto perms = static_cast<uint32_t>(status.permissions());

                    proto::set_permissions(pr_file, perms);
                    builder->make_index(sha256, folder->get_id()).add(pr_file, peer_device).finish().apply(*sup);

                    auto max_seq = folder_info->get_max_sequence();
                    builder->scan_start(folder->get_id()).apply(*sup);

                    CHECK(!bfs::exists(path));
                    CHECK(bfs::exists(model_path));
                    CHECK(read_file(model_path) == "1234567890");
                    CHECK(files->size() == 1);
                    auto f = files->by_name(file_name);
                    REQUIRE(f);
                    CHECK(f->get_size() == 10);
                    REQUIRE(f->iterate_blocks().get_total() == 2);
                    CHECK(f->iterate_blocks(0).next()->get_hash() == hash_1);
                    CHECK(f->iterate_blocks(1).next()->get_hash() == hash_2);
                    CHECK(folder_info->get_max_sequence() == max_seq + 1);

                    builder->scan_start(folder->get_id()).apply(*sup);
                    auto f2 = files->by_name(file_name);
                    CHECK(f2.get() == f.get());
                    REQUIRE(f2->iterate_blocks().get_total() == 2);
                    CHECK(folder_info->get_max_sequence() == max_seq + 1);
                }
                SECTION("1st block match") {
                    write_file(path, "1234500000");
                    auto status = bfs::status(path);
                    auto perms = static_cast<uint32_t>(status.permissions());

                    proto::set_permissions(pr_file, perms);
                    builder->make_index(sha256, folder->get_id()).add(pr_file, peer_device).finish().apply(*sup);

                    auto max_seq = folder_info->get_max_sequence();
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(bfs::exists(path));
                    CHECK(!bfs::exists(model_path));
                    CHECK(files->size() == 0);

                    auto peer_file = folder_info_peer->get_file_infos().by_name(file_name);
                    CHECK(peer_file->iterate_blocks(0).current().first->local_file());
                    CHECK(!peer_file->iterate_blocks(1).current().first->local_file());
                }
                SECTION("2nd block match") {
                    write_file(path, "0000067890");
                    auto status = bfs::status(path);
                    auto perms = static_cast<uint32_t>(status.permissions());

                    proto::set_permissions(pr_file, perms);
                    builder->make_index(sha256, folder->get_id()).add(pr_file, peer_device).finish().apply(*sup);

                    auto max_seq = folder_info->get_max_sequence();
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(bfs::exists(path));
                    CHECK(!bfs::exists(model_path));
                    CHECK(files->size() == 0);

                    auto peer_file = folder_info_peer->get_file_infos().by_name(file_name);
                    CHECK(!peer_file->iterate_blocks(0).current().first->local_file());
                    CHECK(peer_file->iterate_blocks(1).current().first->local_file());
                }
                SECTION("no block match") {
                    write_file(path, "0000000000");
                    auto status = bfs::status(path);
                    auto perms = static_cast<uint32_t>(status.permissions());

                    proto::set_permissions(pr_file, perms);
                    builder->make_index(sha256, folder->get_id()).add(pr_file, peer_device).finish().apply(*sup);

                    auto max_seq = folder_info->get_max_sequence();
                    builder->scan_start(folder->get_id()).apply(*sup);
                    CHECK(!bfs::exists(path));
                    CHECK(!bfs::exists(model_path));
                    CHECK(files->size() == 0);
                }
            }

            CHECK(!folder->get_scan_start().is_special());
            CHECK(!folder->get_scan_finish().is_special());
            CHECK(folder->get_scan_finish() >= folder->get_scan_start());
        }
    };
    F().run();
}

void test_traversal() {
    struct F : fixture_t {
        using paths_t = std::vector<std::string>;

        void on_diff(const model::diff::advance::local_update_t &diff) noexcept override {
            auto file = folder_info->get_file_infos().by_uuid(diff.uuid);
            auto name = file->get_name()->get_full_name();
            paths.emplace_back(std::string(name));
        }

        void main() noexcept override {
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

            builder->scan_start(folder->get_id()).apply(*sup);
            REQUIRE(files->size() == 11);
            REQUIRE(paths.size() == 11);

            // clang-format off
            auto expected = paths_t{
                "a/c/file_2.bin",
                "a/c",
                "a/file.bin",
                "a",
                "b",
                "d/d1/file_3.bin",
                "d/d1",
                "d/d2",
                "d",
                "x.bin",
                "y.bin",
            };
            // clang-format on
            CHECK(paths == expected);
        }
        paths_t paths;
    };
    F().run();
}

void test_importing() {
    struct F : fixture_t {
        void main() noexcept override {
            auto peer_short_id = my_device->device_id().get_uint();
            auto modified_s = std::int64_t{12345};
            auto sha256 = peer_device->device_id().get_sha256();

            SECTION("regular file") {
                auto block_sz = fs::block_sizes[0];
                auto path = root_path / L"файл";

                auto pr_file = proto::FileInfo{};
                auto file_name = boost::nowide::narrow(L"файл");
                proto::set_name(pr_file, file_name);
                proto::set_sequence(pr_file, 4);
                auto &v = proto::get_version(pr_file);
                auto &counter = proto::add_counters(v);
                proto::set_id(counter, 55);
                proto::set_value(counter, peer_short_id);
                proto::set_modified_s(pr_file, modified_s);

                auto data_1 = as_owned_bytes("12345");
                auto data_2 = as_owned_bytes("67890");
                auto hash_1 = utils::sha256_digest(data_1).value();
                auto hash_2 = utils::sha256_digest(data_2).value();
                auto b_1 = proto::BlockInfo();
                proto::set_hash(b_1, hash_1);
                proto::set_size(b_1, data_1.size());
                auto b_2 = proto::BlockInfo();
                proto::set_hash(b_2, hash_2);
                proto::set_offset(b_2, data_1.size());
                proto::set_size(b_2, data_2.size());
                SECTION("one block file") {
                    proto::add_blocks(pr_file, b_1);
                    proto::set_size(pr_file, 5);
                    proto::set_block_size(pr_file, 5);

                    write_file(path, "12345");
                    bfs::last_write_time(path, from_unix(modified_s));

                    proto::set_permissions(pr_file, static_cast<uint32_t>(bfs::status(path).permissions()));
                    builder->make_index(sha256, folder->get_id()).add(pr_file, peer_device).finish().apply(*sup);

                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(files->size() == 1);

                    auto f = *files->begin();
                    REQUIRE(f);
                    CHECK(f->get_size() == proto::get_size(pr_file));
                    REQUIRE(f->get_version().as_proto() == v);
                }
                SECTION("two blocks file (non-standard size)") {
                    proto::add_blocks(pr_file, b_1);
                    proto::add_blocks(pr_file, b_2);
                    proto::set_size(pr_file, 10);
                    proto::set_block_size(pr_file, 5);

                    write_file(path, "1234567890");
                    bfs::last_write_time(path, from_unix(modified_s));

                    proto::set_permissions(pr_file, static_cast<uint32_t>(bfs::status(path).permissions()));
                    builder->make_index(sha256, folder->get_id()).add(pr_file, peer_device).finish().apply(*sup);

                    builder->scan_start(folder->get_id()).apply(*sup);
                    REQUIRE(files->size() == 1);

                    auto f = files->by_name(file_name);
                    REQUIRE(f);
                    CHECK(f->get_size() == proto::get_size(pr_file));
                    REQUIRE(f->get_version().as_proto() == v);
                }
            }
            SECTION("directory") {
                auto path = root_path / L"папка";

                auto pr_file = proto::FileInfo{};
                auto file_name = boost::nowide::narrow(L"папка");
                proto::set_name(pr_file, file_name);
                proto::set_sequence(pr_file, 4);
                proto::set_type(pr_file, proto::FileInfoType::DIRECTORY);
                auto &v = proto::get_version(pr_file);
                auto &counter = proto::add_counters(v);
                proto::set_id(counter, 55);
                proto::set_value(counter, peer_short_id);
                proto::set_modified_s(pr_file, modified_s);
#ifdef SYNCSPIRIT_WIN
                proto::set_no_permissions(pr_file, true);
#endif

                bfs::create_directories(path);
                proto::set_permissions(pr_file, static_cast<uint32_t>(bfs::status(path).permissions()));

                builder->make_index(sha256, folder->get_id()).add(pr_file, peer_device).finish().apply(*sup);

                builder->scan_start(folder->get_id()).apply(*sup);
                REQUIRE(files->size() == 1);

                auto f = files->by_name(file_name);
                REQUIRE(f);
                CHECK(f->get_size() == proto::get_size(pr_file));
                REQUIRE(f->get_version().as_proto() == v);
            }
            SECTION("deleted") {
                SECTION("single deleted file") {
                    auto path = root_path / L"файл.bin";
                    auto pr_file = proto::FileInfo();
                    proto::set_name(pr_file, path.filename().string());
                    proto::set_sequence(pr_file, 4);
                    proto::set_type(pr_file, proto::FileInfoType::FILE);
                    proto::set_deleted(pr_file, true);

                    auto &v = proto::get_version(pr_file);
                    auto &counter = proto::add_counters(v);
                    proto::set_id(counter, 1);
                    proto::set_value(counter, 1);

                    builder->make_index(sha256, folder->get_id()).add(pr_file, peer_device).finish().apply(*sup);

                    builder->scan_start(folder->get_id()).apply(*sup);

                    auto fi_my = folder->get_folder_infos().by_device(*my_device);
                    auto &files_my = fi_my->get_file_infos();
                    REQUIRE(files_my.size() == 1);
                    auto file = files_my.by_name(path.filename().string());
                    REQUIRE(file->get_version().as_proto() == v);
                    CHECK(!bfs::exists(path));
                }
                SECTION("deleted file inside deleted dir") {
                    auto v = proto::Vector();
                    auto &counter = proto::add_counters(v);
                    proto::set_id(counter, 1);
                    proto::set_value(counter, 1);

                    auto dir_path = bfs::path(L"папка");
                    auto file_path = dir_path / L"файл.bin";
                    auto narrow_dir = boost::nowide::narrow(dir_path.generic_wstring());
                    auto narrow_file = boost::nowide::narrow(file_path.generic_wstring());

                    auto pr_dir = proto::FileInfo();
                    proto::set_name(pr_dir, narrow_dir);
                    proto::set_sequence(pr_dir, 4);
                    proto::set_type(pr_dir, proto::FileInfoType::DIRECTORY);
                    proto::set_deleted(pr_dir, true);
                    proto::set_version(pr_dir, v);

                    auto pr_file = proto::FileInfo();
                    proto::set_name(pr_file, narrow_file);
                    proto::set_sequence(pr_file, 5);
                    proto::set_type(pr_file, proto::FileInfoType::FILE);
                    proto::set_deleted(pr_file, true);
                    proto::set_version(pr_file, v);

                    builder->make_index(sha256, folder->get_id())
                        .add(pr_dir, peer_device)
                        .add(pr_file, peer_device)
                        .finish()
                        .apply(*sup);

                    builder->scan_start(folder->get_id()).apply(*sup);

                    auto &files_my = folder_info->get_file_infos();
                    REQUIRE(files_my.size() == 2);

                    auto file_1 = files_my.by_name(narrow_dir);
                    REQUIRE(file_1->get_version().as_proto() == v);
                    auto file_2 = files_my.by_name(narrow_file);
                    REQUIRE(file_2->get_version().as_proto() == v);

                    auto it = bfs::directory_iterator(root_path);
                    auto children_count = std::distance(it, bfs::directory_iterator());
                    CHECK(children_count == 0);
                }
            }
        }
    };
    F().run();
}

void test_concurrency() {
    static constexpr int N = 5;
    static constexpr int M = 3;

    struct F : fixture_t {

        void on_model_update(model::message::model_update_t &) noexcept override { ++local_updates; }

        std::int64_t get_iterations_limit() override { return M; }

        void main() noexcept override {
            for (int i = 0; i < N; ++i) {
                auto letter = static_cast<char>('a' + i);
                auto dir_name = std::string_view(&letter, 1);
                auto dir_path = root_path / "sub-dir" / dir_name;
                bfs::create_directories(dir_path);
                for (int j = 0; j < M; ++j) {
                    auto letter = static_cast<char>('1' + j);
                    auto file_name = std::string_view(&letter, 1);
                    auto file_path = dir_path / file_name;
                    write_file(file_path, "");
                }
            }
            builder->scan_start(folder->get_id()).apply(*sup);
            REQUIRE(files->size() == 1 + N * (M + 1));

            local_updates = 0;
            bfs::remove_all(root_path / "sub-dir");
            builder->scan_start(folder->get_id()).apply(*sup);

            CHECK(local_updates >= N);
            CHECK(local_updates <= N * 2);
        }

        int local_updates = 0;
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_simple, "test_simple", "[net]");
    REGISTER_TEST_CASE(test_no_changes, "test_no_changes", "[net]");
    REGISTER_TEST_CASE(test_deleted, "test_deleted", "[net]");
    REGISTER_TEST_CASE(test_changed, "test_changed", "[net]");
    REGISTER_TEST_CASE(test_type_change, "test_type_change", "[net]");
    REGISTER_TEST_CASE(test_scan_errors, "test_scan_errors", "[net]");
    REGISTER_TEST_CASE(test_read_errors, "test_read_errors", "[net]");
    REGISTER_TEST_CASE(test_leaks, "test_leaks", "[net]");
    REGISTER_TEST_CASE(test_hashing_fail, "test_hashing_fail", "[net]");
    REGISTER_TEST_CASE(test_incomplete, "test_incomplete", "[net]");
    REGISTER_TEST_CASE(test_traversal, "test_traversal", "[net]");
    REGISTER_TEST_CASE(test_importing, "test_importing", "[net]");
    REGISTER_TEST_CASE(test_concurrency, "test_concurrency", "[net]");
    return 1;
}

static int v = _init();
