// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "test-utils.h"
#include "fs/updates_mediator.h"
#include "fs/fs_proxy.h"
#include "fs/utils.h"
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::fs;

using boost::nowide::narrow;

TEST_CASE("block iterator", "[model]") {
    auto root_path = unique_path();
    bfs::create_directories(root_path);
    auto path_guard = test::path_guard_t(root_path);

    auto interval = pt::microseconds{1};
    auto mediator = updates_mediator_t(interval);
    auto deadline = pt::microsec_clock::local_time() + interval;

    auto proxy = fs_proxy_t(mediator, deadline);
    SECTION("create_directories") {
        SECTION("single dir") {
            auto path = root_path / L"папка";
            auto path_str = narrow(path.generic_wstring());
            auto ec = proxy.create_directories(path);
            CHECK(!ec);
            CHECK(mediator.is_masked(path_str) == 1);
            CHECK(proxy.mediator_updates == 1);
        }
    }

    SECTION("open_write") {
        auto path = root_path / L"папка" / L"файл.bin";
        auto path_str = narrow(path.generic_wstring());

        bfs::create_directories(path.parent_path());

        SECTION("empty file") {
            auto f = proxy.open_write(path, 0);
            REQUIRE(f);
            CHECK(mediator.is_masked(path_str) == 2);
            CHECK(proxy.mediator_updates == 2);
            REQUIRE(bfs::exists(path));
            CHECK(bfs::file_size(path) == 0);
        }
        SECTION("non-empty file") {
            auto f = proxy.open_write(path, 10);
            REQUIRE(f);
            CHECK(mediator.is_masked(path_str) == 2);
            CHECK(proxy.mediator_updates == 2);
            REQUIRE(bfs::exists(path));
            CHECK(bfs::file_size(path) == 10);
        }
    }

    SECTION("remove_file") {
        auto path = root_path / L"под-снос";
        auto path_str = narrow(path.generic_wstring());
        write_file(path, "");
        auto ec = proxy.remove(path);
        CHECK(!ec);
        CHECK(mediator.is_masked(path_str) == 1);
        CHECK(proxy.mediator_updates == 1);
        CHECK(!bfs::exists(path));
    }

    SECTION("remove") {
        auto path = root_path / L"под-снос";
        auto path_str = narrow(path.generic_wstring());
        SECTION("file") {
            write_file(path, "");
            auto ec = proxy.remove(path);
            CHECK(!ec);
            CHECK(mediator.is_masked(path_str) == 1);
            CHECK(proxy.mediator_updates == 1);
        }
        SECTION("empty dir") {
            bfs::create_directory(path);
            auto ec = proxy.remove(path);
            CHECK(!ec);
            CHECK(mediator.is_masked(path_str) == 1);
            CHECK(proxy.mediator_updates == 1);
        }
        SECTION("non-dir") {
            bfs::create_directories(path / "bla-bla");
            auto ec = proxy.remove(path);
            CHECK(!ec);
            CHECK(mediator.is_masked(path_str) == 1);
            CHECK(proxy.mediator_updates == 1);
        }
        CHECK(!bfs::exists(path));
    }
    SECTION("last_write_time") {
        auto path = root_path / L"файлик";
        write_file(path, {});
        auto path_str = narrow(path.generic_wstring());
        auto ec = proxy.last_write_time(path, 123456);
        CHECK(!ec);
        CHECK(mediator.is_masked(path_str) == 1);
        CHECK(proxy.mediator_updates == 1);
        auto status = bfs::status(path);
        CHECK(to_unix(bfs::last_write_time(path)) == 123456);
    }
    SECTION("set_perms") {
#ifndef SYNCSPIRIT_WIN
        auto path = root_path / L"файлик";
        write_file(path, {});
        auto path_str = narrow(path.generic_wstring());
        auto ec = proxy.set_perms(path, 0666);
        CHECK(!ec);
        CHECK(mediator.is_masked(path_str) == 1);
        CHECK(proxy.mediator_updates == 1);
        auto status = bfs::status(path);
        CHECK(static_cast<std::uint32_t>(status.permissions()) == 0666);
#endif
    }
    SECTION("create_link") {
#ifndef SYNCSPIRIT_WIN
        auto path = root_path / L"файлик";
        auto target = root_path / L"bla-bla";
        auto path_str = narrow(path.generic_wstring());
        auto ec = proxy.create_link(target, path);
        CHECK(!ec);
        CHECK(mediator.is_masked(path_str) == 1);
        CHECK(proxy.mediator_updates == 1);
        CHECK(bfs::read_symlink(path) == target);
#endif
    }
    SECTION("rename") {
        auto path_1 = root_path / L"файлик1";
        auto path_2 = root_path / L"файлик2";
        write_file(path_1, {});
        auto path_1_str = narrow(path_1.generic_wstring());
        auto path_2_str = narrow(path_2.generic_wstring());
        auto ec = proxy.rename(path_1, path_2);
        CHECK(!ec);
        CHECK(bfs::exists(path_2));
        CHECK(mediator.is_masked(path_1_str) == 0);
        CHECK(mediator.is_masked(path_2_str) == 1);
        CHECK(proxy.mediator_updates == 1);
    }
    SECTION("write") {
        auto path = root_path / L"файл.bin";
        auto path_str = narrow(path.generic_wstring());
        bfs::create_directories(path.parent_path());
        auto opt = proxy.open_write(path, 5);
        REQUIRE(opt);
        auto &f = opt.assume_value();
        mediator.clean_expired();
        CHECK(mediator.is_masked(path_str) == 0);
        proxy.write(path, f, as_bytes("12345"));
        CHECK(mediator.is_masked(path_str) == 2);
    }
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
