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

                p.subscribe_actor(r::lambda<watch_msg_t>([&](watch_msg_t &msg) { on_watch_folder(msg); }));
                p.subscribe_actor(r::lambda<unwatch_msg_t>([&](unwatch_msg_t &msg) { on_unwatch_folder(msg); }));
                p.subscribe_actor(r::lambda<create_dir_msg_t>([&](create_dir_msg_t &msg) { on_create_dir(msg); }));
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

    void on_watch_folder(fs::message::watch_folder_t &msg) override {
        auto &p = msg.payload;
        p.ec = {};
        LOG_DEBUG(log, "watching {}", p.folder_id);
        watched_ack = true;
    }

    virtual void on_unwatch_folder(fs::message::unwatch_folder_t &msg) override {
        watched_ack = false;
        auto &p = msg.payload;
        p.ec = {};
        LOG_DEBUG(log, "unwatching {}", p.folder_id);
    }
    virtual void on_create_dir(fs::message::create_dir_t &msg) override {
        auto &p = msg.payload;
        p.ec = {};
        LOG_DEBUG(log, "creating a dir for {}", p.folder_id, narrow(p.generic_wstring()));
    }

    void prepare(syncspirit_watcher_impl_t impl) noexcept {
        db::Folder db_folder;
        db::set_id(db_folder, folder_id);
        db::set_label(db_folder, folder_id);
        db::set_path(db_folder, "/some/path");
        db::set_watched(db_folder, true);
        builder->upsert_folder(db_folder, 5).apply(*sup);

        folder = cluster->get_folders().by_id(folder_id);
        folder_local = folder->get_folder_infos().by_device(*my_device);
        files_local = &folder_local->get_file_infos();

        launch_target(impl);
        REQUIRE(watched_ack);
    }

    void create_new(proto::FileInfo info) noexcept {
        auto change = fs::payload::file_info_t(std::move(info), {}, fs::update_type_t::created);
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
};

void test_create_changes_zero_size() {
    struct F : folder_fixture_t {
        using parent_t = folder_fixture_t;
        using parent_t::parent_t;
        void main() noexcept override {
            auto impl = GENERATE(I::inotify, I::win32);
            prepare(impl);

            auto file = proto::FileInfo();
            auto file_name = std::string_view("some/file/name");
            proto::set_name(file, file_name);
            proto::set_permissions(file, 0123);
            proto::set_modified_s(file, 12345);

            auto file_type =
                GENERATE(proto::FileInfoType::DIRECTORY, proto::FileInfoType::FILE, proto::FileInfoType::SYMLINK);
            proto::set_type(file, file_type);
            if (file_type == proto::FileInfoType::SYMLINK) {
                proto::set_symlink_target(file, "/some/target");
            }
            create_new(file);

            CHECK(files_local->size() == 1);
            auto f = files_local->by_name(file_name);
            REQUIRE(f);
            CHECK(f->get_permissions() == 0123);
            CHECK(f->get_modified_s() == 12345);
            CHECK(f->get_size() == 0);
            if (file_type == proto::FileInfoType::DIRECTORY) {
                CHECK(f->is_dir());
            }
            if (file_type == proto::FileInfoType::FILE) {
                CHECK(f->is_file());
            }
            if (file_type == proto::FileInfoType::SYMLINK) {
                CHECK(f->is_link());
                CHECK(f->get_link_target() == "/some/target");
            }
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_just_start, "test_just_start", "[fs]");
    REGISTER_TEST_CASE(test_watch_unwatch, "test_watch_unwatch", "[fs]");
    REGISTER_TEST_CASE(test_create_changes_zero_size, "test_create_changes_zero_size", "[fs]");
    return 1;
}

static int v = _init();
