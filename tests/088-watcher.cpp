// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "access.h"
#include "diff-builder.h"
#include "config/fs.h"
#include "fs/fs_context.hpp"
#include "fs/fs_supervisor.h"
#include "fs/watcher_actor.h"
#include "utils/platform.h"
#include <format>
#include <boost/nowide/convert.hpp>
#include "syncspirit-config.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::fs;
using boost::nowide::narrow;

struct SYNCSPIRIT_TEST_API supervisor_t : fs::fs_supervisor_t {
    using parent_t = fs::fs_supervisor_t;
    using parent_t::parent_t;

    void launch_children() noexcept override {
        // NOOP
    }
};

struct fixture_t {
    using target_ptr_t = r::intrusive_ptr_t<fs::watch_actor_t>;
    fixture_t(bool auto_launch = true) noexcept : root_path{unique_path()}, path_guard{root_path} {
        bfs::create_directory(root_path);
    }

    void run() noexcept {
        fs::fs_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();

        sup->start();
        sup->do_process();
        REQUIRE(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

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

    virtual void main() noexcept {}

    bfs::path root_path;
    test::path_guard_t path_guard;
    r::intrusive_ptr_t<supervisor_t> sup;
    target_ptr_t target;
    r::pt::time_duration timeout = r::pt::millisec{10};
};

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

int _init() {
    test::init_logging();
#if defined(SYNCSPIRIT_WATCHER_ANY)
    REGISTER_TEST_CASE(test_start_n_shutdown, "test_start_n_shutdown", "[fs]");
#endif
    return 1;
}

static int v = _init();
