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
#include <optional>

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
    using watch_folder_opt_t = std::optional<fs::payload::watch_folder_t>;
    using unwatch_folder_opt_t = std::optional<fs::payload::unwatch_folder_t>;
    using create_dir_msg_t = r::intrusive_ptr_t<fs::message::create_dir_t>;

    fixture_t() noexcept : root_path{unique_path()}, path_guard{root_path} { bfs::create_directory(root_path); }

    virtual std::uint32_t get_hash_limit() { return 1; }

    virtual std::int64_t get_iterations_limit() { return 100; }

    virtual void on_watch_folder(fs::message::watch_folder_t &msg) {
        CHECK(!watch_folder_payload);
        watch_folder_payload = msg.payload;
    }

    virtual void on_unwatch_folder(fs::message::unwatch_folder_t &msg) {
        CHECK(!unwatch_folder_payload);
        unwatch_folder_payload = msg.payload;
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

    void launch_target(syncspirit_watcher_impl_t impl, bool send_ready = true) {
        target = sup->create_actor<net::local_keeper_t>()
                     .timeout(timeout)
                     .sequencer(sequencer)
                     .concurrent_hashes(get_hash_limit())
                     .files_scan_iteration_limit(get_iterations_limit())
                     .watcher_impl(impl)
                     .finish();
        sup->do_process();

        if (send_ready) {
            sup->send<syncspirit::model::payload::thread_ready_t>(sup->get_address(), cluster,
                                                                  std::this_thread::get_id());
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
    bfs::path root_path;
    test::path_guard_t path_guard;
    target_ptr_t target;
    model::sequencer_ptr_t sequencer;
    watch_folder_opt_t watch_folder_payload;
    unwatch_folder_opt_t unwatch_folder_payload;
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
            launch_target(impl, true);
            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            sup->do_process();

            auto folder_id = "1234-5678";
            db::Folder db_folder;
            db::set_id(db_folder, folder_id);
            db::set_label(db_folder, folder_id);
            db::set_path(db_folder, boost::nowide::narrow(root_path.generic_wstring()));
            db::set_folder_type(db_folder, db::FolderType::send_and_receive);
            SECTION("create non-watched folder") {
                db::set_watched(db_folder, false);
                builder->upsert_folder(db_folder, 5).apply(*sup);
                CHECK(create_dir_msg);
                REQUIRE(!watch_folder_payload);
            }
            SECTION("create watched folder") {
                db::set_watched(db_folder, true);
                builder->upsert_folder(db_folder, 5).apply(*sup);
                REQUIRE(create_dir_msg);
                create_dir_msg->payload.ec = {};
                submit(std::move(create_dir_msg));

                REQUIRE(watch_folder_payload);
                auto &p = watch_folder_payload.value();
                CHECK(p.folder_id == folder_id);
                CHECK(p.path == root_path);
            }
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_just_start, "test_just_start", "[net]");
    REGISTER_TEST_CASE(test_watch_unwatch, "test_watch_unwatch", "[net]");
    return 1;
}

static int v = _init();
