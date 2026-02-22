// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "access.h"
#include "diff-builder.h"
#include "config/fs.h"
#include "fs/fs_slave.h"
#include "fs/messages.h"
#include "fs/utils.h"
#include "model/cluster.h"
#include "net/local_keeper.h"
#include "net/names.h"
#include "test-utils.h"
#include "test_supervisor.h"
#include "access.h"
#include "utils/platform.h"
#include <format>
#include <variant>
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
using boost::nowide::narrow;

struct fixture_t;

using I = syncspirit_watcher_impl_t;
using FT = proto::FileInfoType;

static constexpr auto default_perms = std::uint32_t{0123};
static constexpr auto default_perms_fs = static_cast<bfs::perms>(default_perms);

#ifndef SYNCSPIRIT_WIN
static constexpr auto expected_perms = std::uint32_t{0123};
#else
static constexpr auto expected_perms = std::uint32_t{0666};
#endif

struct my_supervisort_t : supervisor_t {
    using parent_t = supervisor_t;
    using parent_t::parent_t;

    fixture_t *fixture = nullptr;
};

struct fixture_t {
    using target_ptr_t = r::intrusive_ptr_t<net::local_keeper_t>;
    using builder_ptr_t = std::unique_ptr<diff_builder_t>;
    using watch_folder_msg_t = r::intrusive_ptr_t<fs::message::watch_folder_t>;
    using unwatch_folder_msg_t = r::intrusive_ptr_t<fs::message::unwatch_folder_t>;
    using create_dir_msg_t = r::intrusive_ptr_t<fs::message::create_dir_t>;

    fixture_t() noexcept { log = utils::get_logger("fixture"); }

    virtual std::uint32_t get_hash_limit() { return 1; }

    virtual std::int64_t get_iterations_limit() { return 100; }

    virtual void on_watch_folder(fs::message::watch_folder_t &msg) {
        CHECK(!watch_folder_msg);
        watch_folder_msg = &msg;
    }

    virtual void on_unwatch_folder(fs::message::unwatch_folder_t &msg) {
        CHECK(!unwatch_folder_msg);
        unwatch_folder_msg = &msg;
    }
    virtual void on_create_dir(fs::message::create_dir_t &msg) {
        CHECK(!create_dir_msg);
        create_dir_msg = &msg;
    }

    virtual void on_exec(fs::message::foreign_executor_t &msg) { LOG_WARN(log, "on_exec() is not implemented"); }

    void run() noexcept {
        sequencer = make_sequencer(1234);
        auto my_hash = "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD";
        auto my_id = device_id_t::from_string(my_hash).value();
        my_device = device_t::create(my_id, "my-device").value();

        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<my_supervisort_t>()
                  .make_presentation(true)
                  .timeout(timeout)
                  .create_registry()
                  .finish();
        sup->cluster = cluster;
        static_cast<my_supervisort_t *>(sup.get())->fixture = this;
        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
                p.register_name(net::names::fs_actor, sup->get_address());
                p.register_name(net::names::watcher, sup->get_address());
            });
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                using watch_msg_t = fs::message::watch_folder_t;
                using unwatch_msg_t = fs::message::unwatch_folder_t;
                using create_dir_msg_t = fs::message::create_dir_t;
                using exec_msg_t = fs::message::foreign_executor_t;

                p.subscribe_actor(r::lambda<watch_msg_t>([&](watch_msg_t &msg) { on_watch_folder(msg); }));
                p.subscribe_actor(r::lambda<unwatch_msg_t>([&](unwatch_msg_t &msg) { on_unwatch_folder(msg); }));
                p.subscribe_actor(r::lambda<create_dir_msg_t>([&](create_dir_msg_t &msg) { on_create_dir(msg); }));
                p.subscribe_actor(r::lambda<exec_msg_t>([&](exec_msg_t &msg) { on_exec(msg); }));
            });
        };

        sup->start();
        sup->do_process();
        builder = std::make_unique<diff_builder_t>(*cluster);

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        sup->do_process();

        auto fs_config = config::fs_config_t{3600, 10};
        main();

        sup->do_process();
        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    void launch_target(syncspirit_watcher_impl_t impl, bool app_ready = true) {
        target = sup->create_actor<net::local_keeper_t>()
                     .timeout(timeout)
                     .sequencer(sequencer)
                     .concurrent_hashes(get_hash_limit())
                     .files_scan_iteration_limit(get_iterations_limit())
                     .watcher_impl(impl)
                     .finish();
        sup->do_process();

        sup->send<syncspirit::model::payload::thread_ready_t>(sup->get_address(), cluster, std::this_thread::get_id());
        sup->do_process();
        if (app_ready) {
            sup->send<syncspirit::model::payload::app_ready_t>(sup->get_address());
            sup->do_process();
        }
    }

    void submit(r::message_ptr_t message) noexcept {
        message->address = std::move(message->next_route);
        sup->put(message);
        sup->do_process();
    }

    virtual void main() noexcept {}

    std::int64_t files_scan_iteration_limit = 100;
    builder_ptr_t builder;
    r::pt::time_duration timeout = r::pt::millisec{10};
    r::intrusive_ptr_t<supervisor_t> sup;
    cluster_ptr_t cluster;
    device_ptr_t my_device;
    utils::logger_t log;
    target_ptr_t target;
    model::sequencer_ptr_t sequencer;
    watch_folder_msg_t watch_folder_msg;
    unwatch_folder_msg_t unwatch_folder_msg;
    create_dir_msg_t create_dir_msg;
};

void test_just_start() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main() noexcept override {
            auto impl = GENERATE(I::none, I::inotify, I::win32);
            launch_target(impl);
            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            sup->do_process();
        }
    };
    F().run();
}

void test_watch_unwatch() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            auto folder_id = "1234-5678";
            db::Folder db_folder;
            db::set_id(db_folder, folder_id);
            db::set_label(db_folder, folder_id);
            db::set_path(db_folder, "/some/path");
            db::set_folder_type(db_folder, db::FolderType::send_and_receive);

            SECTION("folder is created before start => watched upon app start") {
                db::set_watched(db_folder, true);
                builder->upsert_folder(db_folder, 5).apply(*sup);
                auto folder = cluster->get_folders().by_id(folder_id);
                REQUIRE(!create_dir_msg);

                launch_target(impl, true);

                REQUIRE(!create_dir_msg);
                REQUIRE(watch_folder_msg);
                REQUIRE(!unwatch_folder_msg);
                auto &p = watch_folder_msg->payload;
                CHECK(p.folder_id == folder_id);
                CHECK(p.path == "/some/path");
                p.ec = {};
                submit(std::move(watch_folder_msg));

                SECTION("another upsert") {
                    builder->upsert_folder(db_folder, 5).apply(*sup);
                    REQUIRE(!watch_folder_msg);
                    REQUIRE(!unwatch_folder_msg);
                }

                SECTION("update folder (non-wached) => send unwatch") {
                    db::set_watched(db_folder, false);
                    builder->upsert_folder(db_folder, 5).apply(*sup);
                    CHECK(!watch_folder_msg);
                    REQUIRE(unwatch_folder_msg);
                    auto &p = unwatch_folder_msg->payload;
                    CHECK(p.folder_id == folder_id);
                }
                SECTION("remove folder => send unwatch") {
                    builder->remove_folder(*folder).apply(*sup);
                    REQUIRE(unwatch_folder_msg);
                    auto &p = unwatch_folder_msg->payload;
                    CHECK(p.folder_id == folder_id);
                }
            }
            SECTION("post-start create folder & watch") {
                launch_target(impl);
                CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
                sup->do_process();

                SECTION("create non-watched folder") {
                    db::set_watched(db_folder, false);
                    builder->upsert_folder(db_folder, 5).apply(*sup);
                    CHECK(create_dir_msg);
                    REQUIRE(!watch_folder_msg);
                }

                SECTION("create watched folder") {
                    db::set_watched(db_folder, true);
                    builder->upsert_folder(db_folder, 5).apply(*sup);
                    REQUIRE(create_dir_msg);
                    create_dir_msg->payload.ec = {};
                    submit(std::move(create_dir_msg));

                    REQUIRE(watch_folder_msg);
                    auto &p = watch_folder_msg->payload;
                    CHECK(p.folder_id == folder_id);
                    CHECK(p.path == "/some/path");
                }
            }
        }
    };
    F().run();
}

struct folder_fixture_t : fixture_t {
    using parent_t = fixture_t;
    using parent_t::parent_t;
    using hashed_blocks_t = std::list<utils::bytes_t>;
    using child_infos_t = fs::task::scan_dir_t::child_infos_t;
    using dir_result_t = std::variant<fs::task::scan_dir_t::child_infos_t, sys::error_code>;
    using dir_children_t = std::list<dir_result_t>;

    void on_watch_folder(fs::message::watch_folder_t &msg) override {
        auto &p = msg.payload;
        p.ec = {};
        LOG_DEBUG(log, "watching {}", p.folder_id);
        watched_ack = true;
    }

    void on_unwatch_folder(fs::message::unwatch_folder_t &msg) override {
        watched_ack = false;
        auto &p = msg.payload;
        p.ec = {};
        LOG_DEBUG(log, "unwatching {}", p.folder_id);
    }

    void on_create_dir(fs::message::create_dir_t &msg) override {
        auto &p = msg.payload;
        p.ec = {};
        LOG_DEBUG(log, "creating a dir for {}", p.folder_id, narrow(p.generic_wstring()));
    }

    virtual bool process_cmd(fs::task::scan_dir_t &task) noexcept {
        LOG_DEBUG(log, "process_cmd(scan_dir_t) {}", narrow(task.path.generic_wstring()));
        if (!dir_children.empty()) {
            std::visit(
                [&](auto &item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, sys::error_code>) {
                        task.ec = item;
                        task.child_infos.clear();
                    } else {
                        task.ec = {};
                        task.child_infos = std::move(item);
                    }
                },
                dir_children.front());
            dir_children.pop_front();
            return true;
        }
        return false;
    }

    virtual bool process_cmd(fs::task::segment_iterator_t &task) noexcept {
        static const constexpr size_t SZ = SHA256_DIGEST_LENGTH;
        LOG_DEBUG(log, "process_cmd(segment_iterator_t) {}", narrow(task.path.generic_wstring()));
        for (std::int32_t i = task.block_index, j = 0; j < task.block_count; ++i, ++j) {
            auto bs = (j + 1 == task.block_count) ? task.last_block_size : task.block_size;
            auto off = task.offset + std::int64_t{task.block_size} * j;
            REQUIRE(hashed_blocks.size());
            auto &data = hashed_blocks.front();
            auto tmp_addr = sup->get_registry_address();
            auto dst_addr = target->get_address();
            auto digest = r::make_routed_message<hasher::payload::digest_t>(tmp_addr, dst_addr, data, i, task.context);
            auto digetst_backend = static_cast<hasher::message::digest_t *>(digest.get());
            unsigned char d[SZ];
            utils::digest(data.data(), data.size(), d);
            digetst_backend->payload.result = utils::bytes_t(d, d + SZ);
            hashed_blocks.pop_front();
            sup->put(std::move(digest));
            return true;
        }
        return false;
    }

    virtual bool process_cmd(fs::task::remove_file_t &task) noexcept {
        LOG_DEBUG(log, "process_cmd(remove_file_t) {}", narrow(task.path.generic_wstring()));
        return false;
    }

    virtual bool process_cmd(fs::task::rename_file_t &task) noexcept {
        LOG_DEBUG(log, "process_cmd(rename_file_t) {}", narrow(task.path.generic_wstring()));
        return false;
    }

    virtual bool process_cmd(fs::task::noop_t &task) noexcept {
        LOG_DEBUG(log, "process_cmd(noop_t");
        return false;
    }

    void on_exec(fs::message::foreign_executor_t &msg) override {
        LOG_INFO(log, "on_exec()");
        auto slave = dynamic_cast<fs::fs_slave_t *>(msg.payload.get());
        for (auto &t : slave->tasks_in) {
            auto processed = std::visit([&](auto &task) -> bool { return process_cmd(task); }, t);
            if (processed) {
                slave->ec = {};
            }
        }
        slave->tasks_out = std::move(slave->tasks_in);
    }

    void prepare(syncspirit_watcher_impl_t impl, bool watch_folder = true) noexcept {
        db::Folder db_folder;
        db::set_id(db_folder, folder_id);
        db::set_label(db_folder, folder_id);
        db::set_path(db_folder, "/some/path");
        db::set_watched(db_folder, watch_folder);
        builder->upsert_folder(db_folder, 5).apply(*sup);

        folder = cluster->get_folders().by_id(folder_id);
        folder_local = folder->get_folder_infos().by_device(*my_device);
        files_local = &folder_local->get_file_infos();

        launch_target(impl);
        REQUIRE(watched_ack);
    }

    void expect_bytes_hash(utils::bytes_view_t bytes) noexcept { hashed_blocks.emplace_back(utils::bytes_t(bytes)); }
    void expect_dir_scan(child_infos_t children, bool back = true) noexcept {
        if (back) {
            dir_children.emplace_back(std::move(children));
        } else {
            dir_children.emplace_front(std::move(children));
        }
    }
    void expect_dir_scan_error(sys::error_code ec) noexcept { dir_children.emplace_back(std::move(ec)); }

    void make_update(proto::FileInfo info, fs::update_type_t update_type) noexcept {
        auto change = fs::payload::file_info_t(std::move(info), {}, update_type);
        auto changes = fs::payload::file_changes_t{{std::move(change)}};
        auto folder_change = fs::payload::folder_change_t{folder_id, std::move(changes)};
        auto folder_changes = fs::payload::folder_changes_t{{std::move(folder_change)}};
        auto &addr = sup->get_address();

        sup->send<fs::payload::folder_changes_t>(addr, std::move(folder_changes));
        sup->do_process();
    }

    std::string folder_id = "1234-5678";
    bool watched_ack = false;
    model::folder_ptr_t folder;
    model::folder_info_ptr_t folder_local;
    model::file_infos_map_t *files_local = nullptr;
    hashed_blocks_t hashed_blocks;
    dir_children_t dir_children;
};

void test_trivial_changes() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            prepare(impl);

            auto file = proto::FileInfo();
            auto file_name = std::string_view("some-file-name.bin");
            proto::set_name(file, file_name);
            proto::set_permissions(file, 0123);
            proto::set_modified_s(file, 12345);

            SECTION("create new dir/symlink/emty-file") {
                auto file_type = GENERATE(FT::DIRECTORY, FT::FILE, FT::SYMLINK);
                proto::set_type(file, file_type);
                if (file_type == FT::SYMLINK) {
                    proto::set_symlink_target(file, "/some/target");
                }
                make_update(file, fs::update_type_t::created);

                CHECK(files_local->size() == 1);
                auto f = files_local->by_name(file_name);
                REQUIRE(f);
                CHECK(f->get_permissions() == 0123);
                CHECK(f->get_modified_s() == 12345);
                CHECK(f->get_size() == 0);
                if (file_type == FT::DIRECTORY) {
                    CHECK(f->is_dir());
                }
                if (file_type == FT::FILE) {
                    CHECK(f->is_file());
                }
                if (file_type == FT::SYMLINK) {
                    CHECK(f->is_link());
                    CHECK(f->get_link_target() == "/some/target");
                }
            }
            SECTION("updates on existing") {
                proto::set_type(file, FT::FILE);
                builder->local_update(folder_id, file).apply(*sup);

                auto f = files_local->by_name(file_name);
                CHECK(f->get_permissions() == 0123);
                REQUIRE(f);

                auto file_seq = f->get_sequence();
                auto folder_seq = folder_local->get_max_sequence();
                auto update_type = fs::update_type_t::meta;

                SECTION("update metadata") {
                    proto::set_permissions(file, 0777);
                    update_type = fs::update_type_t::meta;
                    make_update(file, update_type);
                    CHECK(f->get_permissions() == 0777);
                    CHECK(!f->is_deleted());
                }
                SECTION("delete file") {
                    proto::set_deleted(file, true);
                    update_type = fs::update_type_t::deleted;
                    make_update(file, update_type);
                    CHECK(f->get_permissions() == 0123);
                    CHECK(f->is_deleted());
                }
                auto file_seq_2 = f->get_sequence();
                auto folder_seq_2 = folder_local->get_max_sequence();
                CHECK(file_seq_2 > file_seq);
                CHECK(folder_seq_2 > folder_seq);

                make_update(file, update_type);
                CHECK(f->get_sequence() == file_seq_2);
                CHECK(folder_local->get_max_sequence() == folder_seq_2);
            }
        }
    };
    F().run();
}

void test_hashing() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            prepare(impl);

            auto pr_dir_1 = proto::FileInfo();
            proto::set_name(pr_dir_1, narrow(L"папка1"));
            proto::set_type(pr_dir_1, FT::FILE);
            builder->local_update(folder_id, pr_dir_1).apply(*sup);

            auto pr_dir_2 = proto::FileInfo();
            proto::set_name(pr_dir_2, narrow(L"папка1/подпапка2"));
            proto::set_type(pr_dir_2, FT::FILE);
            builder->local_update(folder_id, pr_dir_2).apply(*sup);

            auto pr_file = proto::FileInfo();
            auto file_namew = GENERATE(L"файл.bin", L"папка1/файл.bin", L"папка1/подпапка2/файл.bin");
            auto file_name = narrow(file_namew);
            proto::set_name(pr_file, file_name);
            proto::set_permissions(pr_file, default_perms);
            proto::set_modified_s(pr_file, 12345);
            proto::set_type(pr_file, FT::FILE);
            proto::set_size(pr_file, 5);

            expect_bytes_hash(as_bytes("12345"));
            SECTION("new file created") { make_update(pr_file, fs::update_type_t::created); }
            SECTION("existing file content updated") {
                proto::set_size(pr_file, 4);
                builder->local_update(folder_id, pr_file).apply(*sup);
                make_update(pr_file, fs::update_type_t::content);
            }

            auto f = files_local->by_name(file_name);
            REQUIRE(f);
            CHECK(f->get_permissions() == expected_perms);
            CHECK(f->get_modified_s() == 12345);
            CHECK(f->get_size() == 5);
            CHECK(f->is_file());
        }
    };
    F().run();
}

void test_linux_new_dir() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        using child_info_t = fs::task::scan_dir_t::child_info_t;

        bool process_cmd(fs::task::scan_dir_t &task) noexcept override {
            if (task.notify) {
                ++watcher_notifications;
            }
            return parent_t::process_cmd(task);
        }

        void main() noexcept override {
            prepare(I::inotify);

            auto make_child = [](std::string_view name, bfs::file_type type = bfs::file_type::directory,
                                 std::uintmax_t size = 0, std::uint32_t perms = default_perms) -> child_info_t {
                auto child = child_info_t{};
                child.path = bfs::path(name);
                child.status = bfs::file_status(type, static_cast<bfs::perms>(perms));
                child.size = size;
                return child;
            };

            auto data = as_owned_bytes("12345");
            auto data_h = utils::sha256_digest(data).value();

            auto pr_dir = proto::FileInfo();
            proto::set_name(pr_dir, "dir-a");
            proto::set_permissions(pr_dir, default_perms);
            proto::set_modified_s(pr_dir, 12345);
            proto::set_type(pr_dir, FT::DIRECTORY);

            expect_dir_scan({make_child("/some/path/dir-a/dir-b")});
            expect_dir_scan({make_child("/some/path/dir-a/dir-b/dir-c"),
                             make_child("/some/path/dir-a/dir-b/file.bin", bfs::file_type::regular, 5)});
            expect_dir_scan({});
            expect_bytes_hash(as_bytes("12345"));

            make_update(pr_dir, fs::update_type_t::created);

            auto dir_a = files_local->by_name("dir-a");
            REQUIRE(dir_a);
            CHECK(dir_a->is_dir());
            CHECK(dir_a->is_locally_available());

            auto dir_b = files_local->by_name("dir-a/dir-b");
            REQUIRE(dir_b);
            CHECK(dir_b->is_dir());
            CHECK(dir_b->is_locally_available());

            auto dir_c = files_local->by_name("dir-a/dir-b/dir-c");
            REQUIRE(dir_c);
            CHECK(dir_c->is_dir());
            CHECK(dir_c->is_locally_available());

            auto file = files_local->by_name("dir-a/dir-b/file.bin");
            REQUIRE(file);
            CHECK(file->is_file());
            CHECK(file->is_locally_available());
            CHECK(file->get_size() == 5);
            REQUIRE(file->get_block_size() == 5);

            auto b = file->iterate_blocks().next();
            REQUIRE(b);
            CHECK(b->get_hash() == data_h);
            CHECK(watcher_notifications == 3);
        }

        int watcher_notifications = 0;
    };
    F().run();
}

void test_dir_scan_errors() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        using child_info_t = fs::task::scan_dir_t::child_info_t;

        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            prepare(impl);

            auto make_child = [](std::string_view name) -> child_info_t {
                auto child = child_info_t{};
                child.path = bfs::path(name);
                child.status = bfs::file_status(bfs::file_type::directory);
                return child;
            };

            expect_dir_scan(
                {make_child("/some/path/dir-a/A"), make_child("/some/path/dir-a/B"), make_child("/some/path/dir-a/C")});
            expect_dir_scan({});
            expect_dir_scan_error(utils::make_error_code(utils::error_code_t::no_action));
            expect_dir_scan({});

            auto pr_dir = proto::FileInfo();
            proto::set_name(pr_dir, "dir-a");
            proto::set_permissions(pr_dir, default_perms);
            proto::set_modified_s(pr_dir, 12345);
            proto::set_type(pr_dir, FT::DIRECTORY);

            if (impl == I::inotify) {
                SECTION("new dir/update") {
                    make_update(pr_dir, fs::update_type_t::created);

                    auto dir_a = files_local->by_name("dir-a");
                    REQUIRE(dir_a);
                    CHECK(dir_a->is_dir());
                    CHECK(dir_a->is_locally_available());

                    auto dir_A = files_local->by_name("dir-a/A");
                    REQUIRE(dir_A);
                    CHECK(dir_A->is_dir());
                    CHECK(dir_A->is_locally_available());

                    auto dir_B = files_local->by_name("dir-a/B");
                    CHECK(!dir_B);

                    auto dir_C = files_local->by_name("dir-a/C");
                    REQUIRE(dir_C);
                    CHECK(dir_C->is_dir());
                    CHECK(dir_C->is_locally_available());
                }
            }
            SECTION("existing dirs") {
                for (auto &p : {"dir-a", "dir-a/A", "dir-a/B", "dir-a/C"}) {
                    proto::set_name(pr_dir, p);
                    builder->local_update(folder_id, pr_dir).apply(*sup);
                }

                auto data = as_bytes("12345");
                auto data_h = utils::sha256_digest(as_bytes("12345")).value();

                auto b = proto::BlockInfo();
                proto::set_hash(b, data_h);
                proto::set_offset(b, 0);
                proto::set_size(b, 5);
                proto::set_name(pr_dir, "dir-a/B/x.bin");
                proto::set_type(pr_dir, proto::FileInfoType::FILE);
                proto::set_size(pr_dir, 5);
                proto::add_blocks(pr_dir, b);

                expect_bytes_hash(data);
                builder->local_update(folder_id, pr_dir).apply(*sup);

                expect_dir_scan({make_child("/some/path/dir-a")}, false);
                LOG_INFO(log, "triggering scan...");
                builder->scan_start(folder_id).apply(*sup);

                auto dir_a = files_local->by_name("dir-a");
                REQUIRE(dir_a);
                CHECK(dir_a->is_dir());
                CHECK(dir_a->is_locally_available());

                auto dir_A = files_local->by_name("dir-a/A");
                REQUIRE(dir_A);
                CHECK(dir_A->is_dir());
                CHECK(dir_A->is_locally_available());

                auto dir_B = files_local->by_name("dir-a/B");
                REQUIRE(dir_B);
                CHECK(dir_B->is_dir());
                CHECK(dir_B->is_locally_available());
                CHECK(dir_B->is_unreachable());

                auto file_X = files_local->by_name("dir-a/B/x.bin");
                REQUIRE(file_X);
                CHECK(file_X->is_file());
                CHECK(!file_X->is_locally_available());
                CHECK(file_X->is_unreachable());

                auto dir_C = files_local->by_name("dir-a/C");
                REQUIRE(dir_C);
                CHECK(dir_C->is_dir());
                CHECK(dir_C->is_locally_available());
            }
        }

        int watcher_notifications = 0;
    };
    F().run();
}

void test_read_file_errors() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        using child_info_t = fs::task::scan_dir_t::child_info_t;

        bool process_cmd(fs::task::segment_iterator_t &task) noexcept override {
            LOG_DEBUG(log, "process_cmd(segment_iterator_t) {}", narrow(task.path.generic_wstring()));
            task.ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return true;
        }

        std::uint32_t get_hash_limit() override { return concurrency; }

        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            concurrency = GENERATE(1, 5, 10, 100);
            prepare(impl);

            auto multiplier = GENERATE(1, 3, 5, 11);

            auto block_sz = fs::block_sizes[0];
            auto child = child_info_t{};
            child.path = bfs::path("/some/path/file.bin");
            child.status = bfs::file_status(bfs::file_type::regular);
            child.size = block_sz * multiplier;

            expect_dir_scan({child});
            LOG_INFO(log, "triggering scan...");
            builder->scan_start(folder_id).apply(*sup);
            CHECK(files_local->size() == 0);
        }

        std::uint32_t concurrency = 1;
    };
    F().run();
};

void test_read_file_errors_partial() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        using child_info_t = fs::task::scan_dir_t::child_info_t;

        bool process_cmd(fs::task::segment_iterator_t &task) noexcept override {
            static const constexpr size_t SZ = SHA256_DIGEST_LENGTH;
            LOG_DEBUG(log, "process_cmd(segment_iterator_t) {}, error index = {}", narrow(task.path.generic_wstring()),
                      error_index);

            for (std::int32_t i = task.block_index, j = 0; j < task.block_count; ++i, ++j) {
                if (i == error_index) {
                    task.ec = std::make_error_code(std::errc::no_such_file_or_directory);
                } else {
                    auto bs = (j + 1 == task.block_count) ? task.last_block_size : task.block_size;
                    auto off = task.offset + std::int64_t{task.block_size} * j;
                    auto data = as_owned_bytes(std::string(fs::block_sizes[0], 'a' + i));
                    auto tmp_addr = sup->get_registry_address();
                    auto dst_addr = target->get_address();
                    auto digest =
                        r::make_routed_message<hasher::payload::digest_t>(tmp_addr, dst_addr, data, i, task.context);
                    auto digetst_backend = static_cast<hasher::message::digest_t *>(digest.get());
                    unsigned char d[SZ];
                    utils::digest(data.data(), data.size(), d);
                    digetst_backend->payload.result = utils::bytes_t(d, d + SZ);
                    sup->put(std::move(digest));
                }
            }

            return true;
        }

        std::uint32_t get_hash_limit() override { return concurrency; }

        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            concurrency = GENERATE(1, 2, 3, 4, 5);
            error_index = GENERATE(0, 1, 2, 3, 4);
            prepare(impl);

            auto multiplier = 5;

            auto block_sz = fs::block_sizes[0];
            auto child = child_info_t{};
            child.path = bfs::path("/some/path/file.bin");
            child.status = bfs::file_status(bfs::file_type::regular);
            child.size = block_sz * multiplier;

            expect_dir_scan({child});
            LOG_INFO(log, "triggering scan...");
            builder->scan_start(folder_id).apply(*sup);
            CHECK(files_local->size() == 0);
        }

        std::uint32_t concurrency = 1;
        std::uint32_t error_index = 0;
    };
    F().run();
};

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_just_start, "test_just_start", "[fs]");
    REGISTER_TEST_CASE(test_watch_unwatch, "test_watch_unwatch", "[fs]");
    REGISTER_TEST_CASE(test_trivial_changes, "test_trivial_changes", "[fs]");
    REGISTER_TEST_CASE(test_hashing, "test_hashing", "[fs]");
    REGISTER_TEST_CASE(test_linux_new_dir, "test_linux_new_dir", "[fs]");
    REGISTER_TEST_CASE(test_dir_scan_errors, "test_dir_scan_errors", "[fs]");
    REGISTER_TEST_CASE(test_read_file_errors, "test_read_file_errors", "[fs]");
    REGISTER_TEST_CASE(test_read_file_errors_partial, "test_read_file_errors_partial", "[fs]");
    return 1;
}

static int v = _init();
