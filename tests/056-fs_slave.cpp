// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "test-utils.h"
#include "fs/fs_slave.h"
#include "fs/utils.h"
#include "fs/updates_mediator.h"
#include "test-utils.h"
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::model;
using namespace syncspirit::fs;
using boost::nowide::narrow;

struct exec_ctx_t final : fs::execution_context_t {
    exec_ctx_t(fs::updates_mediator_t *mediator_ = nullptr) { mediator = mediator_; }
    pt::ptime get_deadline() const override { return clock_t::local_time() + pt::milliseconds{1}; };
};

TEST_CASE("fs_slave, scan_dir", "[fs]") {
    auto root_path = unique_path();
    bfs::create_directories(root_path);
    test::path_guard_t path_quard{root_path};

    auto slave = fs_slave_t();
    auto context = exec_ctx_t();
#if 0
    auto timeout = r::pt::time_duration(r::pt::millisec{10});
    r::system_context_t ctx;
    auto sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
    sup->do_process();
#endif

    SECTION("dir scan") {
        SECTION("empty dir") {
            slave.push(task::scan_dir_t(root_path, {}, {}));
            CHECK(!slave.exec(context));
            REQUIRE(slave.tasks_out.size() == 1);
            auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
            CHECK(!t.ec);
            CHECK(t.child_infos.size() == 0);
        }
        SECTION("non-existing dir") {
            slave.push(task::scan_dir_t(root_path / "non-existing", {}, {}));
            slave.exec(context);
            REQUIRE(slave.tasks_out.size() == 1);
            auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
            CHECK(t.ec);
            CHECK(t.ec.message() != "");
        }
        SECTION("not a dir") {
            slave.push(task::scan_dir_t(root_path / "file", {}, {}));
            write_file(root_path / "file", "");
            slave.exec(context);
            REQUIRE(slave.tasks_out.size() == 1);
            auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
            CHECK(t.ec);
            CHECK(t.ec.message() != "");
        }
        SECTION("not a dir") {
            slave.push(task::scan_dir_t(root_path / "file", {}, {}));
            write_file(root_path / "file", "");
            slave.exec(context);
            REQUIRE(slave.tasks_out.size() == 1);
            auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
            CHECK(t.ec);
            CHECK(t.ec.message() != "");
        }

#ifndef SYNCSPIRIT_WIN
        SECTION("dir with a file, dir & symlink") {
            slave.push(task::scan_dir_t(root_path, {}, {}));

            auto modified = std::int64_t{1642007468};
            auto child_1 = root_path / L"a_файл";
            auto child_2 = root_path / L"b_ссылка";
            auto child_3 = root_path / L"с_папка";
            auto link_target = bfs::path("/some/where");

            write_file(child_1, "abc");
            bfs::last_write_time(child_1, from_unix(modified));
            bfs::create_symlink(link_target, child_2);
            bfs::create_directories(child_3);
            bfs::last_write_time(child_3, from_unix(modified));

            slave.exec(context);

            REQUIRE(slave.tasks_out.size() == 1);
            auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
            CHECK(!t.ec);
            CHECK(t.child_infos.size() == 3);
            {
                auto &c = t.child_infos[0];
                CHECK(c.path.filename() == child_2.filename());
                CHECK(!c.ec);
                CHECK(c.status.type() == bfs::file_type::symlink);
                CHECK(c.target == link_target);
            }
            {
                auto &c = t.child_infos[1];
                CHECK(c.path.filename() == child_1.filename());
                CHECK(!c.ec);
                CHECK(c.status.type() == bfs::file_type::regular);
                CHECK(to_unix(c.last_write_time) == modified);
                CHECK(c.size == 3);
            }
            {
                auto &c = t.child_infos[2];
                CHECK(c.path.filename() == child_3.filename());
                CHECK(!c.ec);
                CHECK(c.status.type() == bfs::file_type::directory);
                CHECK(to_unix(c.last_write_time) == modified);
            }
        }
#endif
    }

#if 0
    sup->do_shutdown();
    sup->do_process();
#endif
}

TEST_CASE("fs_slave, rm_file", "[fs]") {
    auto root_path = unique_path();
    bfs::create_directories(root_path);
    test::path_guard_t path_quard{root_path};
    auto slave = fs_slave_t();
    auto mediator = fs::updates_mediator_t(pt::milliseconds{1});
    auto context = exec_ctx_t(&mediator);

    SECTION("successfuly remove") {
        auto file = root_path / "file";
        write_file(file, "");
        slave.push(task::remove_file_t(file));
        CHECK(slave.exec(context));
        REQUIRE(slave.tasks_out.size() == 1);
        auto &t = std::get<task::remove_file_t>(slave.tasks_out.front());
        CHECK(!t.ec);
        CHECK(!bfs::exists(file));
        auto path_str = narrow(file.generic_wstring());
        CHECK(mediator.is_masked(path_str));
    }

    SECTION("failed to remove") {
        auto file = root_path / "dir";
        bfs::create_directories(file / "subdir");
        slave.push(task::remove_file_t(file));
        CHECK(!slave.exec(context));
        REQUIRE(slave.tasks_out.size() == 1);
        auto &t = std::get<task::remove_file_t>(slave.tasks_out.front());
        CHECK(t.ec);
        CHECK(t.ec.message() != "");
        CHECK(bfs::exists(file));
    }
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
