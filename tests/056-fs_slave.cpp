// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "test-utils.h"
#include "fs/fs_slave.h"
#include "fs/updates_mediator.h"
#include "fs/fs_proxy.h"
#include "test-utils.h"
#include "test_supervisor.h"
#include "syncspirit-config.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::model;
using namespace syncspirit::fs;

inline static auto retension = pt::milliseconds{1};

struct exec_ctx_t final : fs::execution_context_t {
    exec_ctx_t() : mediator(retension, true), proxy_holder(mediator, clock_t::local_time() + retension) {
        fs_proxy = &proxy_holder;
    }

    fs::updates_mediator_t mediator;
    fs::fs_proxy_t proxy_holder;
};

struct my_supervisor_t final : test::supervisor_t {
    using parent_t = test::supervisor_t;
    using parent_t::parent_t;
};

TEST_CASE("fs_slave, scan_dir", "[fs]") {
    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto path_quard = unique_path();
    auto root_path = path_quard.get_view(allocator);

    auto slave = fs_slave_t();
    auto context = exec_ctx_t();

    SECTION("dir scan") {
        SECTION("empty dir") {
            slave.push(task::scan_dir_t(root_path.detach(), {}, {}, false, true, false));
            CHECK(!slave.exec(context));
            REQUIRE(slave.tasks_out.size() == 1);
            auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
            CHECK(!t.ec);
            CHECK(t.child_infos.size() == 0);
        }
        SECTION("non-existing dir") {
            slave.push(task::scan_dir_t((root_path / "non-existing").detach(), {}, {}, false, true, false));
            slave.exec(context);
            REQUIRE(slave.tasks_out.size() == 1);
            auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
            CHECK(t.ec);
            CHECK(t.ec.message() != "");
            CHECK(t.ec == std::errc::no_such_file_or_directory);
        }
        SECTION("not a dir") {
            slave.push(task::scan_dir_t((root_path / "file").detach(), {}, {}, false, true, false));
            write_file(root_path / "file", "");
            slave.exec(context);
            REQUIRE(slave.tasks_out.size() == 1);
            auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
            CHECK(t.ec);
            CHECK(t.ec.message() != "");
        }
        SECTION("dir with 2 children") {
            write_file(root_path / "file-1", "");
            write_file(root_path / "file-2", "");

            SECTION("scan whole dir") {
                slave.push(task::scan_dir_t(root_path.detach(), {}, {}, false, true, false));
                slave.exec(context);
                REQUIRE(slave.tasks_out.size() == 1);
                auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
                CHECK(!t.ec);
                CHECK(t.child_infos.size() == 2);
            }
            SECTION("scan whole dir with callback") {
                bool invoked = false;
                context.scan_dir_callback = [&](auto &) { invoked = true; };
                slave.push(task::scan_dir_t(root_path.detach(), {}, {}, true, true, false));
                slave.exec(context);
                REQUIRE(slave.tasks_out.size() == 1);
                auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
                CHECK(!t.ec);
                CHECK(t.child_infos.size() == 2);
                CHECK(invoked);
            }
            SECTION("single child scan (1)") {
                slave.push(
                    task::scan_dir_t(root_path.detach(), {}, utils::path_t::make_native("file-1"), false, true, false));
                slave.exec(context);
                REQUIRE(slave.tasks_out.size() == 1);
                auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
                CHECK(!t.ec);
                CHECK(t.child_infos.size() == 1);
            }
            SECTION("single child scan (2)") {
                slave.push(
                    task::scan_dir_t(root_path.detach(), {}, utils::path_t::make_native("file-x"), false, true, false));
                slave.exec(context);
                REQUIRE(slave.tasks_out.size() == 1);
                auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
                CHECK(!t.ec);
                CHECK(t.child_infos.size() == 0);
            }
        }

#ifndef SYNCSPIRIT_WIN
        SECTION("dir with a file, dir & symlink") {
            slave.push(task::scan_dir_t(root_path.detach(), {}, {}, false, true, false));

            auto modified = std::int64_t{1642007468};
            auto child_1 = root_path / L"a_файл";
            auto child_2 = root_path / L"b_ссылка";
            auto child_3 = root_path / L"с_папка";
            auto link_target = utils::path_t::make_native("/some/where");

            write_file(child_1, "abc");
            last_write_time(child_1, modified);
            create_symlink(link_target, child_2);
            create_directories(child_3);
            last_write_time(child_3, modified);

            slave.exec(context);

            REQUIRE(slave.tasks_out.size() == 1);
            auto &t = std::get<task::scan_dir_t>(slave.tasks_out.front());
            CHECK(!t.ec);
            CHECK(t.child_infos.size() == 3);
            {
                auto &c = t.child_infos[0];
                CHECK(c.path.get_filename() == child_2.get_filename());
                CHECK(!c.ec);
                CHECK(c.file_type == utils::file_type_t::SYMLINK);
                CHECK(c.target == link_target);
            }
            {
                auto &c = t.child_infos[1];
                CHECK(c.path.get_filename() == child_1.get_filename());
                CHECK(!c.ec);
                CHECK(c.file_type == utils::file_type_t::FILE);
                CHECK(c.last_write_time == modified);
                CHECK(c.size == 3);
            }
            {
                auto &c = t.child_infos[2];
                CHECK(c.path.get_filename() == child_3.get_filename());
                CHECK(!c.ec);
                CHECK(c.file_type == utils::file_type_t::DIRECTORY);
                CHECK(c.last_write_time == modified);
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
    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto path_quard = unique_path();
    auto root_path = path_quard.get_view(allocator);

    auto slave = fs_slave_t();
    auto context = exec_ctx_t();
    auto &mediator = context.mediator;

    SECTION("successfuly remove") {
        auto file = root_path / "file";
        write_file(file, "");
        slave.push(task::remove_file_t(file.detach()));
        CHECK(slave.exec(context));
        REQUIRE(slave.tasks_out.size() == 1);
        auto &t = std::get<task::remove_file_t>(slave.tasks_out.front());
        CHECK(!t.ec);
        CHECK(!exists(file));
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
        CHECK(mediator.is_masked(file.get_full_name()));
#else
        CHECK(mediator.is_masked(root_path.get_full_name()));
#endif
    }

    SECTION("failed to remove") {
        auto file = root_path / "dir";
        create_directories(file / "subdir");
        slave.push(task::remove_file_t(file.detach()));
        CHECK(!slave.exec(context));
        REQUIRE(slave.tasks_out.size() == 1);
        auto &t = std::get<task::remove_file_t>(slave.tasks_out.front());
        CHECK(t.ec);
        CHECK(t.ec.message() != "");
        CHECK(exists(file));
    }
}

TEST_CASE("fs_slave, segment-iterator (errors only)", "[fs]") {
    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto path_quard = unique_path();
    auto root_path = path_quard.get_view(allocator);

    auto slave = fs_slave_t();
    auto context = exec_ctx_t();
    auto back_addr = r::address_ptr_t();
    auto hash_context = hasher::payload::extendended_context_prt_t();
    hash_context = new hasher::payload::extendended_context_t();

    SECTION("attempt to read a dir") {
        auto task = task::segment_iterator_t(back_addr, hash_context, root_path.detach(), 0, 0, 1, 5, 5, 0);
        slave.push(std::move(task));
        CHECK(!slave.exec(context));
        REQUIRE(slave.tasks_out.size() == 1);
        auto &t = std::get<task::segment_iterator_t>(slave.tasks_out.front());
        CHECK(t.ec);
        CHECK(t.ec.message() != "");
    };
    SECTION("attempt to non-existing dir") {
        auto path = root_path / "not-existing-file";
        auto task = task::segment_iterator_t(back_addr, hash_context, path.detach(), 0, 0, 1, 5, 5, 0);
        slave.push(std::move(task));
        CHECK(!slave.exec(context));
        REQUIRE(slave.tasks_out.size() == 1);
        auto &t = std::get<task::segment_iterator_t>(slave.tasks_out.front());
        CHECK(t.ec);
        CHECK(t.ec.message() != "");
    };
#ifndef SYNCSPIRIT_WIN
    SECTION("concurrent file modification") {
        auto path = root_path / "file.bin";
        write_file(path, "12345");
        auto modified = last_write_time(path);
        last_write_time(path, modified - 10);

        auto task = fs::task::segment_iterator_t(back_addr, hash_context, path.detach(), 0, 0, 1, 5, 5, modified);
        slave.push(std::move(task));
        CHECK(!slave.exec(context));
        REQUIRE(slave.tasks_out.size() == 1);
        auto &t = std::get<task::segment_iterator_t>(slave.tasks_out.front());
        CHECK(t.ec);
        CHECK(t.ec.message() != "");
        CHECK(t.ec == utils::error_code_t::concurrent_file_modification);
    };
#endif
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
