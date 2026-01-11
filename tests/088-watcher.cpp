// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "access.h"
#include "diff-builder.h"
#include "config/fs.h"
#include "fs/fs_context.hpp"
#include "fs/fs_supervisor.h"
#include "fs/watcher_actor.h"
#include "utils/error_code.h"
#include "utils/platform.h"
#include <format>
#include <boost/nowide/convert.hpp>
#include "syncspirit-config.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::fs;
using boost::nowide::narrow;

struct fixture_t;

struct supervisor_t : fs::fs_supervisor_t {
    using parent_t = fs::fs_supervisor_t;
    using parent_t::parent_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        parent_t::configure(plugin);
        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) { p.subscribe_actor(&supervisor_t::on_watch); });
    }

    void launch_children() noexcept override {
        // NOOP
    }

    void on_watch(message::watch_folder_t &) noexcept;

    fixture_t *fixture;
};

struct fixture_t {
    using target_ptr_t = r::intrusive_ptr_t<fs::watch_actor_t>;
    using fs_context_ptr_r = r::intrusive_ptr_t<fs::fs_context_t>;

    fixture_t(bool auto_launch_ = true) noexcept
        : auto_launch{auto_launch_}, root_path{unique_path()}, path_guard{root_path} {
        bfs::create_directory(root_path);
    }

    void run() noexcept {
        fs_context.reset(new fs::fs_context_t());
        sup = fs_context->create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->fixture = this;

        sup->start();
        sup->do_process();
        REQUIRE(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        if (auto_launch) {
            launch_target();
            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
        }

        main();

        sup->do_process();
        sup->do_shutdown();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    void launch_target() {
        target = sup->create_actor<target_ptr_t::element_type>().timeout(timeout).finish();
        sup->do_process();
    }

    void poll() {
        fs_context->wait_next_event();
        sup->do_process();
    }

    virtual void on_watch(message::watch_folder_t &msg) noexcept {
        CHECK(!msg.payload.ec);
        CHECK(msg.payload.ec.message() == "Success");
        ++watched_replies;
    }

    virtual void main() noexcept {}

    bool auto_launch;
    bfs::path root_path;
    test::path_guard_t path_guard;
    fs_context_ptr_r fs_context;
    r::intrusive_ptr_t<supervisor_t> sup;
    target_ptr_t target;
    r::pt::time_duration timeout = r::pt::millisec{10};
    size_t watched_replies = 0;
};

void supervisor_t::on_watch(message::watch_folder_t &msg) noexcept { fixture->on_watch(msg); }

void test_start_n_shutdown() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        void main() noexcept override {
            launch_target();
            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

            target->do_shutdown();
            sup->do_process();
            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::SHUT_DOWN);
            REQUIRE(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);
        }
    };
    F(false).run();
}

void test_flat_root_folder() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main() noexcept override {
            auto back_addr = sup->get_address();
            auto ec = utils::make_error_code(utils::error_code_t::no_action);
            sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, ec);
            sup->do_process();
            REQUIRE(watched_replies == 1);

            SECTION("new dir") {
                auto path = root_path / "my-dir";
                bfs::create_directories(path);
                poll();
            }
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
#if defined(SYNCSPIRIT_WATCHER_ANY)
    REGISTER_TEST_CASE(test_start_n_shutdown, "test_start_n_shutdown", "[fs]");
    REGISTER_TEST_CASE(test_flat_root_folder, "test_flat_root_folder", "[fs]");
#endif
    return 1;
}

static int v = _init();
