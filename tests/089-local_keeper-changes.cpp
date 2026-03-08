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
        local_device = device_t::create(my_id, "my-device").value();

        cluster = new cluster_t(local_device, 1);

        cluster->get_devices().put(local_device);

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
    device_ptr_t local_device;
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
    using child_info_t = fs::task::scan_dir_t::child_info_t;

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

    static child_info_t make_child(std::string_view name, bfs::file_type type = bfs::file_type::directory,
                                   std::uintmax_t size = 0, std::uint32_t perms = default_perms,
                                   std::int64_t modified = 0) {
        auto child = child_info_t{};
        child.path = bfs::path(name);
        child.status = bfs::file_status(type, static_cast<bfs::perms>(perms));
        child.size = size;
        child.last_write_time = fs::from_unix(modified);
        return child;
    };

    virtual bool process_cmd(fs::task::scan_dir_t &task) noexcept {
        auto path = narrow(task.path.generic_wstring());
        if (!dir_children.empty()) {
            std::visit(
                [&](auto &item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, sys::error_code>) {
                        task.ec = item;
                        LOG_DEBUG(log, "process_cmd(scan_dir_t) '{}' -> error: {}", path, item.message());
                        task.child_infos.clear();
                    } else {
                        task.ec = {};
                        LOG_DEBUG(log, "process_cmd(scan_dir_t) '{}' -> {} items", path, item.size());
                        task.child_infos = std::move(item);
                    }
                },
                dir_children.front());
            dir_children.pop_front();
            return true;
        } else {
            LOG_DEBUG(log, "process_cmd(scan_dir_t) '{}' -> no results", path);
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
        folder_local = folder->get_folder_infos().by_device(*local_device);
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

    void make_update(proto::FileInfo info, fs::update_type_t update_type, bool process = true) noexcept {
        auto change = fs::payload::file_info_t(std::move(info), {}, update_type);
        auto changes = fs::payload::file_changes_t{{std::move(change)}};
        auto folder_change = fs::payload::folder_change_t{folder_id, std::move(changes)};
        auto folder_changes = fs::payload::folder_changes_t{{std::move(folder_change)}};
        auto &addr = sup->get_address();

        sup->send<fs::payload::folder_changes_t>(addr, std::move(folder_changes));
        if (process) {
            sup->do_process();
        }
    }

    void make_update_rename(proto::FileInfo info, std::string_view prev_name, bool process = true) noexcept {
        auto change = fs::payload::file_info_t(std::move(info), std::string(prev_name), fs::update_type_t::meta);
        auto changes = fs::payload::file_changes_t{{std::move(change)}};
        auto folder_change = fs::payload::folder_change_t{folder_id, std::move(changes)};
        auto folder_changes = fs::payload::folder_changes_t{{std::move(folder_change)}};
        auto &addr = sup->get_address();

        sup->send<fs::payload::folder_changes_t>(addr, std::move(folder_changes));
        if (process) {
            sup->do_process();
        }
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
            LOG_INFO(log, "impl: {}", static_cast<int>(impl));

            auto file = proto::FileInfo();
            auto file_name = std::string_view("some-file-name.bin");
            proto::set_name(file, file_name);
            proto::set_permissions(file, default_perms);
            proto::set_modified_s(file, 12345);

            SECTION("create new dir/symlink/emty-file") {
                auto file_type = GENERATE(FT::DIRECTORY, FT::FILE, FT::SYMLINK);
                proto::set_type(file, file_type);
                if (file_type == FT::SYMLINK) {
                    proto::set_symlink_target(file, "/some/target");
                }
                if (file_type == FT::DIRECTORY) {
                    expect_dir_scan({});
                }
                make_update(file, fs::update_type_t::created);

                CHECK(files_local->size() == 1);
                auto f = files_local->by_name(file_name);
                REQUIRE(f);
#ifndef SYNCSPIRIT_WIN
                CHECK(f->get_permissions() == expected_perms);
#endif
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
            auto from = task.block_index;
            auto to = from + task.block_count;
            LOG_DEBUG(log, "process_cmd(segment_iterator_t) {}[{}..{}], error index = {}",
                      narrow(task.path.generic_wstring()), from, to, error_index);

            if (error_index >= from && error_index < to) {
                task.ec = std::make_error_code(std::errc::io_error);
            } else {
                for (std::int32_t i = from, j = 0; j < task.block_count; ++i, ++j) {
                    using digest_t = hasher::payload::digest_t;
                    auto bs = (j + 1 == task.block_count) ? task.last_block_size : task.block_size;
                    auto off = task.offset + std::int64_t{task.block_size} * j;
                    auto data = as_owned_bytes(std::string(fs::block_sizes[0], 'a' + i));
                    auto tmp_addr = sup->get_registry_address();
                    auto dst_addr = target->get_address();
                    auto digest = r::make_routed_message<digest_t>(tmp_addr, dst_addr, data, i, task.context);
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
            LOG_INFO(log, "triggering scan... concurrency: {}, error_index: {}", concurrency, error_index);
            builder->scan_start(folder_id).apply(*sup);
            CHECK(files_local->size() == 0);
        }

        std::uint32_t concurrency = 1;
        std::uint32_t error_index = 0;
    };
    F().run();
};

void test_read_file_error_recovery() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        using child_info_t = fs::task::scan_dir_t::child_info_t;

        bool process_cmd(fs::task::segment_iterator_t &task) noexcept override {
            static const constexpr size_t SZ = SHA256_DIGEST_LENGTH;
            auto from = task.block_index;
            auto to = from + task.block_count;
            LOG_DEBUG(log, "process_cmd(segment_iterator_t) {}[{}..{}], error index = {}",
                      narrow(task.path.generic_wstring()), from, to, error_index);

            if (error_index >= from && error_index < to) {
                task.ec = std::make_error_code(std::errc::io_error);
            } else {
                for (std::int32_t i = from, j = 0; j < task.block_count; ++i, ++j) {
                    using digest_t = hasher::payload::digest_t;
                    auto bs = (j + 1 == task.block_count) ? task.last_block_size : task.block_size;
                    auto off = task.offset + std::int64_t{task.block_size} * j;
                    auto data = as_owned_bytes(std::string(fs::block_sizes[0], 'a' + i));
                    auto tmp_addr = sup->get_registry_address();
                    auto dst_addr = target->get_address();
                    auto digest = r::make_routed_message<digest_t>(tmp_addr, dst_addr, data, i, task.context);
                    auto digetst_backend = static_cast<hasher::message::digest_t *>(digest.get());
                    unsigned char d[SZ];
                    utils::digest(data.data(), data.size(), d);
                    digetst_backend->payload.result = utils::bytes_t(d, d + SZ);
                    sup->put(std::move(digest));
                }
                blocks_read += task.block_count;
            }

            return true;
        }

        std::uint32_t get_hash_limit() override { return concurrency; }

        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            error_index = 3;
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
            CHECK(blocks_read == 0);

            error_index = 99;

            expect_dir_scan({child});
            LOG_INFO(log, "triggering scan...");
            builder->scan_start(folder_id).apply(*sup);
            CHECK(files_local->size() == 1);
            auto file = files_local->begin()->get();
            REQUIRE(file);
            REQUIRE(file->get_size() == child.size);
            REQUIRE(file->iterate_blocks().get_total() == 5);
            CHECK(blocks_read == 5);
        }

        std::uint32_t concurrency = 4;
        std::uint32_t error_index = 0;
        std::uint32_t blocks_read = 0;
    };
    F().run();
};

void test_duplicates() {
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

            auto file_type = GENERATE(FT::DIRECTORY, FT::FILE, FT::SYMLINK);
            proto::set_type(file, file_type);
            if (file_type == FT::SYMLINK) {
                proto::set_symlink_target(file, "/some/target");
            }
            make_update(file, fs::update_type_t::created);

            auto seq = folder_local->get_max_sequence();
            make_update(file, fs::update_type_t::created);
            CHECK(folder_local->get_max_sequence() == seq);

            proto::set_modified_s(file, 123456);
            make_update(file, fs::update_type_t::meta);
            CHECK(folder_local->get_max_sequence() == seq + 1);

            make_update(file, fs::update_type_t::meta);
            CHECK(folder_local->get_max_sequence() == seq + 1);
        }
    };
    F().run();
}

void test_multi_folders_update() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        using child_info_t = fs::task::scan_dir_t::child_info_t;

        void main() noexcept override {
            for (auto folder_id : {"p1", "p2", "p3"}) {
                db::Folder db_folder;
                db::set_id(db_folder, folder_id);
                db::set_label(db_folder, folder_id);
                db::set_path(db_folder, fmt::format("/some/{}", folder_id));
                db::set_folder_type(db_folder, db::FolderType::send_and_receive);
                db::set_watched(db_folder, true);
                builder->upsert_folder(db_folder, 5).apply(*sup);
            }

            auto impl = GENERATE(I::inotify, I::win32);
            launch_target(impl);

            expect_dir_scan({make_child("/some/p1/A")});
            expect_dir_scan({make_child("/some/p1/A/a1"), make_child("/some/p1/A/a2"), make_child("/some/p1/A/a3")});
            expect_dir_scan({});
            expect_dir_scan({});
            expect_dir_scan({});

            expect_dir_scan({make_child("/some/p2/B")});
            expect_dir_scan({make_child("/some/p2/B/b1"), make_child("/some/p2/B/b2"), make_child("/some/p2/B/b3")});
            expect_dir_scan({});
            expect_dir_scan({});
            expect_dir_scan({});

            expect_dir_scan({make_child("/some/p3/C")});
            expect_dir_scan({make_child("/some/p3/C/c1"), make_child("/some/p3/C/c2"), make_child("/some/p3/C/c3")});
            expect_dir_scan({});
            expect_dir_scan({});
            expect_dir_scan({});

            struct D {
                std::string_view folder_id;
                std::initializer_list<std::string_view> names;
            };

            D changes[] = {{"p1", {"A", "A/a1", "A/a2", "A/a3"}},
                           {"p2", {"B", "B/b1", "B/b2", "B/b3"}},
                           {"p3", {"C", "C/c1", "C/c2", "C/c3"}}};

            auto folder_changes = fs::payload::folder_changes_t{};
            for (auto &c : changes) {
                auto file_changes = fs::payload::file_changes_t{};
                for (auto &name : c.names) {
                    auto file = proto::FileInfo();
                    proto::set_name(file, name);
                    proto::set_permissions(file, default_perms);
                    proto::set_modified_s(file, 12345);
                    proto::set_type(file, FT::DIRECTORY);
                    auto change = fs::payload::file_info_t(file, {}, fs::update_type_t::created);
                    file_changes.emplace_back(std::move(change));
                }
                auto folder_id = std::string(c.folder_id);
                auto folder_change = fs::payload::folder_change_t{folder_id, std::move(file_changes)};
                folder_changes.emplace_back(std::move(folder_change));
            }

            auto &addr = sup->get_address();
            sup->send<fs::payload::folder_changes_t>(addr, std::move(folder_changes));
            sup->do_process();

            auto &folders_map = cluster->get_folders();
            auto f1 = folders_map.by_id("p1");
            auto f2 = folders_map.by_id("p2");
            auto f3 = folders_map.by_id("p3");

            auto &f1_files = f1->get_folder_infos().by_device(*local_device)->get_file_infos();
            auto f_A = f1_files.by_name("A");
            auto f_a1 = f1_files.by_name("A/a1");
            auto f_a2 = f1_files.by_name("A/a2");
            auto f_a3 = f1_files.by_name("A/a3");
            CHECK(f_A);
            CHECK(f_a1);
            CHECK(f_a2);
            CHECK(f_a3);

            auto &f2_files = f2->get_folder_infos().by_device(*local_device)->get_file_infos();
            auto f_B = f2_files.by_name("B");
            auto f_b1 = f2_files.by_name("B/b1");
            auto f_b2 = f2_files.by_name("B/b2");
            auto f_b3 = f2_files.by_name("B/b3");
            CHECK(f_B);
            CHECK(f_b1);
            CHECK(f_b2);
            CHECK(f_b3);

            auto &f3_files = f3->get_folder_infos().by_device(*local_device)->get_file_infos();
            auto f_C = f3_files.by_name("C");
            auto f_c1 = f3_files.by_name("C/c1");
            auto f_c2 = f3_files.by_name("C/c2");
            auto f_c3 = f3_files.by_name("C/c3");
            CHECK(f_C);
            CHECK(f_c1);
            CHECK(f_c2);
            CHECK(f_c3);
        }
    };
    F().run();
}

void test_hierarchy_update_dirs_only() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;

        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            prepare(impl);

            expect_dir_scan({make_child("/some/path/dir/subdir")});
            expect_dir_scan({});

            auto folder_changes = fs::payload::folder_changes_t{};
            auto file_changes = fs::payload::file_changes_t{};
            for (auto &name : {"dir", "dir/subdir"}) {
                auto file = proto::FileInfo();
                proto::set_name(file, name);
                proto::set_permissions(file, default_perms);
                proto::set_modified_s(file, 12345);
                proto::set_type(file, FT::DIRECTORY);
                auto change = fs::payload::file_info_t(file, {}, fs::update_type_t::created);
                file_changes.emplace_back(std::move(change));
            }
            auto folder_change = fs::payload::folder_change_t{folder_id, std::move(file_changes)};
            folder_changes.emplace_back(std::move(folder_change));

            auto &addr = sup->get_address();
            sup->send<fs::payload::folder_changes_t>(addr, std::move(folder_changes));
            sup->do_process();

            CHECK(files_local->size() == 2);
            auto d = files_local->by_name("dir");
            CHECK(d);

            auto subdir = files_local->by_name("dir/subdir");
            CHECK(subdir);
        }
    };
    F().run();
}

void test_hierarchy_update_with_content() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;

        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            // auto impl = I::win32;
            prepare(impl);

            expect_dir_scan({make_child("/some/path/dir/file.bin", bfs::file_type::regular, 5)});
            expect_dir_scan({});
            expect_bytes_hash(as_bytes("12345"));

            auto folder_changes = fs::payload::folder_changes_t{};
            auto file_changes = fs::payload::file_changes_t{};
            [&]() {
                auto file = proto::FileInfo();
                proto::set_name(file, "dir");
                proto::set_permissions(file, default_perms);
                proto::set_modified_s(file, 12345);
                proto::set_type(file, FT::DIRECTORY);
                auto change = fs::payload::file_info_t(file, {}, fs::update_type_t::created);
                file_changes.emplace_back(std::move(change));
            }();
            [&]() {
                auto file = proto::FileInfo();
                proto::set_name(file, "dir/file.bin");
                proto::set_permissions(file, default_perms);
                proto::set_modified_s(file, 12345);
                proto::set_type(file, FT::FILE);
                proto::set_size(file, 5);
                auto change = fs::payload::file_info_t(file, {}, fs::update_type_t::created);
                file_changes.emplace_back(std::move(change));
            }();
            auto folder_change = fs::payload::folder_change_t{folder_id, std::move(file_changes)};
            folder_changes.emplace_back(std::move(folder_change));

            auto &addr = sup->get_address();
            sup->send<fs::payload::folder_changes_t>(addr, std::move(folder_changes));
            sup->do_process();

            CHECK(files_local->size() == 2);
            auto d = files_local->by_name("dir");
            CHECK(d);

            auto f = files_local->by_name("dir/file.bin");
            REQUIRE(f);
            CHECK(f->get_size() == 5);
            CHECK(f->iterate_blocks().get_total() == 1);
            CHECK(f->is_locally_available());
        }
    };
    F().run();
}

void test_malformed_hierarchy_update() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;

        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            prepare(impl);

            expect_dir_scan({make_child("/some/path/dir/file.bin", bfs::file_type::regular, 5)});
            expect_dir_scan({});
            expect_bytes_hash(as_bytes("12345"));

            auto folder_changes = fs::payload::folder_changes_t{};
            auto file_changes = fs::payload::file_changes_t{};
            [&]() {
                auto file = proto::FileInfo();
                proto::set_name(file, "dir/file.bin");
                proto::set_permissions(file, default_perms);
                proto::set_modified_s(file, 12345);
                proto::set_type(file, FT::FILE);
                proto::set_size(file, 5);
                auto change = fs::payload::file_info_t(file, {}, fs::update_type_t::created);
                file_changes.emplace_back(std::move(change));
            }();
            auto folder_change = fs::payload::folder_change_t{folder_id, std::move(file_changes)};
            folder_changes.emplace_back(std::move(folder_change));

            auto &addr = sup->get_address();
            sup->send<fs::payload::folder_changes_t>(addr, std::move(folder_changes));
            sup->do_process();

            CHECK(files_local->size() == 0);
        }
    };
    F().run();
}

void test_scan_dirs_race_linux() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;

        bool process_cmd(fs::task::scan_dir_t &task) noexcept override {
            if (task.path == bfs::path("/some/path/a")) {
                LOG_INFO(log, "mix-in update");
                auto pr_dir = proto::FileInfo();
                proto::set_name(pr_dir, "x");
                proto::set_permissions(pr_dir, default_perms);
                proto::set_modified_s(pr_dir, 12345);
                proto::set_type(pr_dir, FT::DIRECTORY);
                make_update(pr_dir, fs::update_type_t::created, false);

                proto::set_name(pr_dir, "y");
                make_update(pr_dir, fs::update_type_t::created, false);
            }
            return parent_t::process_cmd(task);
        }

        void main() noexcept override {
            prepare(I::inotify);

            expect_dir_scan({make_child("/some/path/a")});
            expect_dir_scan({make_child("/some/path/a/b")});
            expect_dir_scan({make_child("/some/path/a/b/c")});
            expect_dir_scan({});
            expect_dir_scan({make_child("/some/path/x/x1")});
            expect_dir_scan({make_child("/some/path/x/x1/x2")});
            expect_dir_scan({});
            expect_dir_scan({make_child("/some/path/y/y1")});
            expect_dir_scan({make_child("/some/path/y/y1/y2")});
            expect_dir_scan({});

            LOG_INFO(log, "triggering scan...");
            builder->scan_start(folder_id).apply(*sup);

            CHECK(files_local->size() == 9);
            CHECK(folder_local->get_max_sequence() == 9);
        }
    };
    F().run();
}

void test_scan_dirs_race_linux_2() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;

        void main() noexcept override {
            prepare(I::inotify);

            expect_dir_scan({make_child("/some/path/a")});
            expect_dir_scan({});
            LOG_INFO(log, "triggering scan (1)...");
            builder->scan_start(folder_id).apply(*sup);
            REQUIRE(files_local->size() == 1);

            auto pr_dir = proto::FileInfo();
            proto::set_name(pr_dir, "x");
            proto::set_permissions(pr_dir, default_perms);
            proto::set_modified_s(pr_dir, 12345);
            proto::set_type(pr_dir, FT::DIRECTORY);
            make_update(pr_dir, fs::update_type_t::created, false);

            proto::set_name(pr_dir, "y");
            make_update(pr_dir, fs::update_type_t::created, false);

            expect_dir_scan({});
            expect_dir_scan({});
            expect_dir_scan({make_child("/some/path/a"), make_child("/some/path/x"), make_child("/some/path/y")});
            expect_dir_scan({});
            expect_dir_scan({});
            expect_dir_scan({make_child("/some/path/a/b")});
            expect_dir_scan({make_child("/some/path/a/b/c")});
            expect_dir_scan({});

            LOG_INFO(log, "triggering scan (2)...");
            builder->scan_start(folder_id).apply(*sup);

            CHECK(files_local->size() == 5);
            CHECK(folder_local->get_max_sequence() == 5);
        }
    };
    F().run();
}

void test_scan_dirs_race_win32() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;

        bool process_cmd(fs::task::scan_dir_t &task) noexcept override {
            if (task.path == bfs::path("/some/path/a")) {
                LOG_INFO(log, "mix-in update");
                auto pr_dir = proto::FileInfo();
                proto::set_permissions(pr_dir, default_perms);
                proto::set_modified_s(pr_dir, 12345);
                proto::set_type(pr_dir, FT::DIRECTORY);

                for (auto name : {"x", "x/x1", "x/x1/x2", "y", "y/y1", "y/y1/y2"}) {
                    proto::set_name(pr_dir, name);
                    make_update(pr_dir, fs::update_type_t::created, false);
                }
            }
            return parent_t::process_cmd(task);
        }

        void main() noexcept override {
            prepare(I::win32);

            expect_dir_scan({make_child("/some/path/a")});
            expect_dir_scan({make_child("/some/path/a/b")});
            expect_dir_scan({make_child("/some/path/a/b/c")});
            expect_dir_scan({});

            LOG_INFO(log, "triggering scan...");
            builder->scan_start(folder_id).apply(*sup);

            CHECK(files_local->size() == 9);
            CHECK(folder_local->get_max_sequence() == 9);
        }
    };
    F().run();
}

void test_scan_dirs_race_win32_2() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;

        void main() noexcept override {
            prepare(I::win32);

            expect_dir_scan({make_child("/some/path/a")});
            expect_dir_scan({make_child("/some/path/a/b")});
            expect_dir_scan({make_child("/some/path/a/b/c")});
            expect_dir_scan({});
            LOG_INFO(log, "triggering scan (1)...");
            builder->scan_start(folder_id).apply(*sup);
            REQUIRE(files_local->size() == 3);

            auto pr_dir = proto::FileInfo();
            proto::set_name(pr_dir, "x");
            proto::set_permissions(pr_dir, default_perms);
            proto::set_modified_s(pr_dir, 12345);
            proto::set_type(pr_dir, FT::DIRECTORY);
            make_update(pr_dir, fs::update_type_t::created, false);

            proto::set_name(pr_dir, "y");
            make_update(pr_dir, fs::update_type_t::created, false);

            expect_dir_scan({make_child("/some/path/a"), make_child("/some/path/x"), make_child("/some/path/y")});
            expect_dir_scan({});
            expect_dir_scan({});
            expect_dir_scan({make_child("/some/path/a/b")});
            expect_dir_scan({make_child("/some/path/a/b/c")});
            expect_dir_scan({});

            LOG_INFO(log, "triggering scan (2)...");
            builder->scan_start(folder_id).apply(*sup);

            CHECK(files_local->size() == 5);
            CHECK(folder_local->get_max_sequence() == 5);
        }
    };
    F().run();
}

void test_hashing_race() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        using trigger_t = std::function<void()>;

        bool process_cmd(fs::task::segment_iterator_t &task) noexcept override {
            auto r = parent_t::process_cmd(task);
            if (trigger) {
                trigger();
                trigger = {};
            }
            return r;
        }

        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            prepare(impl);

            auto number = std::uint32_t{5};
            auto sz = fs::block_sizes[0] * number;
            expect_dir_scan({make_child("/some/path/file.bin", bfs::file_type::regular, sz, default_perms, 12345)});

            auto block_a = as_owned_bytes(std::string(fs::block_sizes[0], 'a'));
            auto block_b = as_owned_bytes(std::string(fs::block_sizes[0], 'b'));
            auto hash_a = utils::sha256_digest(block_a).value();
            auto hash_b = utils::sha256_digest(block_b).value();

            SECTION("scan, then update") {
                for (std::uint32_t i = 0; i < number; ++i) {
                    expect_bytes_hash(block_a);
                }
                for (std::uint32_t i = 0; i < number; ++i) {
                    expect_bytes_hash(block_b);
                }

                LOG_INFO(log, "triggering scan...");
                trigger = [&]() {
                    LOG_INFO(log, "triggering update...");
                    auto pr_file = proto::FileInfo();
                    proto::set_name(pr_file, "file.bin");
                    proto::set_permissions(pr_file, default_perms);
                    proto::set_modified_s(pr_file, 12348);
                    proto::set_type(pr_file, FT::FILE);
                    proto::set_size(pr_file, sz);
                    make_update(pr_file, fs::update_type_t::content, false);
                };

                builder->scan_start(folder_id).apply(*sup);
                CHECK(folder_local->get_max_sequence() == 2);
            }
            SECTION("update, then scan") {
                for (std::uint32_t i = 0; i < number; ++i) {
                    expect_bytes_hash(block_b);
                }
                LOG_INFO(log, "triggering update...");
                auto pr_file = proto::FileInfo();
                proto::set_name(pr_file, "file.bin");
                proto::set_permissions(pr_file, default_perms);
                proto::set_modified_s(pr_file, 12345);
                proto::set_type(pr_file, FT::FILE);
                proto::set_size(pr_file, sz);
                make_update(pr_file, fs::update_type_t::content, false);
                builder->scan_start(folder_id).apply(*sup);
                CHECK(folder_local->get_max_sequence() == 1);
            }
            REQUIRE(files_local->size() == 1);
            auto f = files_local->by_name("file.bin");
            REQUIRE(f);
            REQUIRE(f->get_size() == sz);
            REQUIRE(f->iterate_blocks().get_total() == number);
            for (auto it = f->iterate_blocks(); it.current().first; it.next()) {
                auto b = it.current().first->get_hash();
                CHECK(b == hash_b);
            }
            CHECK(hashed_blocks.size() == 0);
        }

        trigger_t trigger;
    };
    F().run();
}

void test_renaming_simple() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        using trigger_t = std::function<void()>;

        void main() noexcept override {
            prepare(I::inotify);

            {
                auto dir = proto::FileInfo();
                proto::set_name(dir, narrow(L"папка"));
                proto::set_permissions(dir, default_perms);
                proto::set_modified_s(dir, 12345);
                proto::set_type(dir, FT::DIRECTORY);
                builder->local_update(folder_id, dir).apply(*sup);
            }

            auto file_name_1 = narrow(L"папка/файл-1.bin");
            auto file_name_2 = narrow(L"папка/файл-2.bin");
            auto file = proto::FileInfo();
            proto::set_permissions(file, default_perms);
            proto::set_modified_s(file, 12345);
            proto::set_name(file, file_name_1);

            SECTION("empty dir/file/link") {
                auto file_type = GENERATE(FT::DIRECTORY, FT::FILE, FT::SYMLINK);
                proto::set_type(file, file_type);
                if (file_type == FT::SYMLINK) {
                    proto::set_symlink_target(file, "/some/target");
                }
                if (file_type == FT::DIRECTORY) {
                    expect_dir_scan({});
                }
                builder->local_update(folder_id, file).apply(*sup);

                proto::set_name(file, file_name_2);

                auto seq_1 = folder_local->get_max_sequence();
                make_update_rename(file, file_name_1);

                CHECK(folder_local->get_max_sequence() > seq_1);

                auto f2 = files_local->by_name(file_name_2);
                REQUIRE(f2);
                CHECK(f2->get_permissions() == default_perms);
                CHECK(f2->get_modified_s() == 12345);
                CHECK(!f2->is_deleted());
                CHECK(model::file_info_t::as_type(f2->get_type()) == file_type);

                auto f1 = files_local->by_name(file_name_1);
                REQUIRE(f1);
                CHECK(f1->get_permissions() == default_perms);
                CHECK(f1->get_modified_s() == 12345);
                CHECK(f1->is_deleted());
            }

            SECTION("non-empty file") {
                proto::set_type(file, FT::FILE);
                auto data = as_bytes("12345");
                auto data_h = utils::sha256_digest(as_bytes("12345")).value();

                auto b = proto::BlockInfo();
                proto::set_hash(b, data_h);
                proto::set_offset(b, 0);
                proto::set_size(b, 5);
                proto::set_size(file, 5);
                proto::add_blocks(file, b);

                builder->local_update(folder_id, file).apply(*sup);
                auto &blocks = cluster->get_blocks();
                REQUIRE(blocks.size() == 1);

                proto::set_name(file, file_name_2);

                auto seq_1 = folder_local->get_max_sequence();
                make_update_rename(file, file_name_1);

                CHECK(folder_local->get_max_sequence() > seq_1);

                auto f2 = files_local->by_name(file_name_2);
                REQUIRE(f2);
                CHECK(f2->get_permissions() == default_perms);
                CHECK(f2->get_modified_s() == 12345);
                CHECK(!f2->is_deleted());
                CHECK(model::file_info_t::as_type(f2->get_type()) == FT::FILE);
                CHECK(f2->get_block_size() == 5);
                CHECK(f2->iterate_blocks(0).get_total() == 1);
                CHECK(f2->iterate_blocks(0).current().first->get_hash() == data_h);

                auto f1 = files_local->by_name(file_name_1);
                REQUIRE(f1);
                CHECK(f1->get_permissions() == default_perms);
                CHECK(f1->get_modified_s() == 12345);
                CHECK(f1->is_deleted());
                CHECK(blocks.size() == 1);
            }
        }
    };
    F().run();
}

void test_renaming_hierarchy() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        using trigger_t = std::function<void()>;

        void main() noexcept override {
            prepare(I::inotify);

            auto data_1 = as_bytes("12345");
            auto data1_h = utils::sha256_digest(data_1).value();

            auto data_2 = as_bytes("67890");
            auto data2_h = utils::sha256_digest(data_2).value();

            auto data_3 = as_bytes("55555");
            auto data3_h = utils::sha256_digest(data_3).value();

            auto b1 = proto::BlockInfo();
            proto::set_hash(b1, data1_h);
            proto::set_offset(b1, 0);
            proto::set_size(b1, 5);

            auto b2 = proto::BlockInfo();
            proto::set_hash(b2, data2_h);
            proto::set_offset(b2, 0);
            proto::set_size(b2, 5);

            auto b3 = proto::BlockInfo();
            proto::set_hash(b3, data2_h);
            proto::set_offset(b3, 5);
            proto::set_size(b3, 5);
            {
                auto pr = proto::FileInfo();
                proto::set_permissions(pr, default_perms);
                proto::set_modified_s(pr, 12345);
                proto::set_type(pr, FT::DIRECTORY);
                auto names = {
                    L"папка", L"папка/sd", L"папка/sd/f1.bin", L"папка/sd/f2.bin", L"папка/kept",
                };
                for (auto &name_w : names) {
                    auto name = narrow(name_w);
                    proto::set_name(pr, name);
                    proto::clear_blocks(pr);
                    if (name.find("f1") != std::string::npos) {
                        proto::set_type(pr, FT::FILE);
                        proto::set_size(pr, 5);
                        proto::set_block_size(pr, 5);
                        proto::add_blocks(pr, b1);
                    }
                    if (name.find("f2") != std::string::npos) {
                        proto::set_type(pr, FT::FILE);
                        proto::set_size(pr, 10);
                        proto::set_block_size(pr, 5);
                        proto::add_blocks(pr, b2);
                        proto::add_blocks(pr, b3);
                    }
                    builder->local_update(folder_id, pr).apply(*sup);
                }
            }

            auto file_name_1 = narrow(L"папка/sd");
            auto file_name_2 = narrow(L"папка/подпапка");
            auto pr = proto::FileInfo();
            proto::set_permissions(pr, default_perms);
            proto::set_modified_s(pr, 12345);
            proto::set_type(pr, FT::DIRECTORY);
            proto::set_name(pr, file_name_2);
            make_update_rename(pr, file_name_1);

            auto ex_names = {L"папка/sd", L"папка/sd/f1.bin", L"папка/sd/f2.bin"};
            for (auto &name_w : ex_names) {
                auto name = narrow(name_w);
                auto f = files_local->by_name(name);
                REQUIRE(f);
                log->debug("f = {}", f->get_name()->get_full_name());
                CHECK(f->is_deleted());
            }

            auto exising_names = {L"папка", L"папка/подпапка", L"папка/подпапка/f1.bin", L"папка/подпапка/f2.bin",
                                  L"папка/kept"};
            for (auto &name_w : exising_names) {
                auto name = narrow(name_w);
                log->debug("f = {}", name);
                auto f = files_local->by_name(name);
                REQUIRE(f);
                CHECK(!f->is_deleted());
            }
        }
    };
    F().run();
}

void test_renaming_race() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        using trigger_t = std::function<void()>;
        using rename_pair_t = std::pair<std::string_view, std::string_view>;

        void main() noexcept override {
            prepare(I::inotify);

            {
                auto pr = proto::FileInfo();
                proto::set_permissions(pr, default_perms);
                proto::set_modified_s(pr, 12345);
                proto::set_type(pr, FT::DIRECTORY);
                auto names = {"dir_a0", "dir_a0/b0", "dir_a0/c0"};
                for (auto &name : names) {
                    proto::set_name(pr, name);
                    builder->local_update(folder_id, pr).apply(*sup);
                }
            }
            CHECK(folder_local->get_max_sequence() == 3);

            // clang-format off
            auto pairs = {
                rename_pair_t{"dir_a0/b0", "dir_a0/b1"},
                rename_pair_t{"dir_a0", "dir_a2"},
                rename_pair_t{"dir_a2/c0", "dir_a2/c3"},
                rename_pair_t{"dir_a2/b1", "dir_a2/b4"},
            };
            // clang-format on

            auto changes = fs::payload::file_changes_t{};
            for (auto &[from, to] : pairs) {
                auto pr = proto::FileInfo();
                proto::set_permissions(pr, default_perms);
                proto::set_modified_s(pr, 12345);
                proto::set_type(pr, FT::DIRECTORY);
                proto::set_name(pr, to);
                auto change = fs::payload::file_info_t(std::move(pr), std::string(from), fs::update_type_t::meta);
                changes.push_back(std::move(change));
            }
            auto folder_change = fs::payload::folder_change_t{folder_id, std::move(changes)};
            auto folder_changes = fs::payload::folder_changes_t{{std::move(folder_change)}};
            auto &addr = sup->get_address();
            sup->send<fs::payload::folder_changes_t>(addr, std::move(folder_changes));
            sup->do_process();

            CHECK(folder_local->get_max_sequence() == 15);
            auto deleted = {"dir_a0/b0", "dir_a0", "dir_a0/b1", "dir_a0/c0", "dir_a2/c0", "dir_a2/b1"};
            auto prev_seq = std::int64_t{4};
            for (auto &name : deleted) {
                auto f = files_local->by_name(name);
                log->debug("f = {}", name);
                REQUIRE(f);
                CHECK(f->is_deleted());
                CHECK(prev_seq < f->get_sequence());
                prev_seq = f->get_sequence();
            }

            auto existing = {"dir_a2", "dir_a2/b4", "dir_a2/c3"};
            for (auto &name : existing) {
                auto f = files_local->by_name(name);
                log->debug("f = {}", name);
                REQUIRE(f);
                CHECK(!f->is_deleted());
            }
        }
    };
    F().run();
}

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
    REGISTER_TEST_CASE(test_read_file_error_recovery, "test_read_file_error_recovery", "[fs]");
    REGISTER_TEST_CASE(test_duplicates, "test_duplicates", "[fs]");
    REGISTER_TEST_CASE(test_multi_folders_update, "test_multi_folders_update", "[fs]");
    REGISTER_TEST_CASE(test_hierarchy_update_dirs_only, "test_hierarchy_update_dirs_only", "[fs]");
    REGISTER_TEST_CASE(test_hierarchy_update_with_content, "test_hierarchy_update_with_content", "[fs]");
    REGISTER_TEST_CASE(test_malformed_hierarchy_update, "test_malformed_hierarchy_update", "[fs]");
    REGISTER_TEST_CASE(test_scan_dirs_race_linux, "test_scan_dirs_race_linux", "[fs]");
    REGISTER_TEST_CASE(test_scan_dirs_race_linux_2, "test_scan_dirs_race_linux_2", "[fs]");
    REGISTER_TEST_CASE(test_scan_dirs_race_win32, "test_scan_dirs_race_win32", "[fs]");
    REGISTER_TEST_CASE(test_scan_dirs_race_win32_2, "test_scan_dirs_race_win32_2", "[fs]");
    REGISTER_TEST_CASE(test_hashing_race, "test_hashing_race", "[fs]");
    REGISTER_TEST_CASE(test_renaming_simple, "test_renaming_simple", "[fs]");
    REGISTER_TEST_CASE(test_renaming_hierarchy, "test_renaming_hierarchy", "[fs]");
    REGISTER_TEST_CASE(test_renaming_race, "test_renaming_race", "[fs]");
    return 1;
}

static int v = _init();
