// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "test-utils.h"
#include "fs/fs_slave.h"
#include "fs/utils.h"
#include "test_supervisor.h"
#include "test-utils.h"
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::model;
using namespace syncspirit::fs;

namespace {
struct sup_t final : r::supervisor_t {
    using r::supervisor_t::supervisor_t;
};

} // namespace

TEST_CASE("fs_slave", "[fs]") {
    auto root_path = unique_path();
    bfs::create_directories(root_path);
    test::path_guard_t path_quard{root_path};

    auto timeout = r::pt::time_duration(r::pt::millisec{10});
    auto slave = fs_slave_t();
    r::system_context_t ctx;
    auto sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
    sup->do_process();

    SECTION("dir scan"){SECTION("empty dir"){slave.push(task::scan_dir_t(root_path));
    slave.exec(*sup);
    REQUIRE(slave.tasks_out.size() == 1);
    auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
    CHECK(!t.ec);
    CHECK(t.child_infos.size() == 0);
}
SECTION("non-existing dir") {
    slave.push(task::scan_dir_t(root_path / "non-existing"));
    slave.exec(*sup);
    REQUIRE(slave.tasks_out.size() == 1);
    auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
    CHECK(t.ec);
    CHECK(t.ec.message() != "");
}
SECTION("not a dir") {
    slave.push(task::scan_dir_t(root_path / "file"));
    write_file(root_path / "file", "");
    slave.exec(*sup);
    REQUIRE(slave.tasks_out.size() == 1);
    auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
    CHECK(t.ec);
    CHECK(t.ec.message() != "");
}
SECTION("not a dir") {
    slave.push(task::scan_dir_t(root_path / "file"));
    write_file(root_path / "file", "");
    slave.exec(*sup);
    REQUIRE(slave.tasks_out.size() == 1);
    auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
    CHECK(t.ec);
    CHECK(t.ec.message() != "");
}

#ifndef SYNCSPIRIT_WIN
SECTION("dir with a file, dir & symlink") {
    slave.push(task::scan_dir_t(root_path));

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

    slave.exec(*sup);

    REQUIRE(slave.tasks_out.size() == 1);
    auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
    CHECK(!t.ec);
    CHECK(t.child_infos.size() == 3);
    {
        auto &c = t.child_infos[0];
        CHECK(c.path.filename() == child_3.filename());
        CHECK(!c.ec);
        CHECK(c.status.type() == bfs::file_type::directory);
        CHECK(c.last_write_time == from_unix(modified));
    }
    {
        auto &c = t.child_infos[1];
        CHECK(c.path.filename() == child_1.filename());
        CHECK(!c.ec);
        CHECK(c.status.type() == bfs::file_type::regular);
        CHECK(c.last_write_time == from_unix(modified));
        CHECK(c.size == 3);
    }
    {
        auto &c = t.child_infos[2];
        CHECK(c.path.filename() == child_2.filename());
        CHECK(!c.ec);
        CHECK(c.status.type() == bfs::file_type::symlink);
        CHECK(c.target == link_target);
    }
}
#endif
}

sup->do_shutdown();
sup->do_process();
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
