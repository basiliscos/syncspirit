// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "test-utils.h"
#include "syncspirit-config.h"
#include "fs/updates_mediator.h"
#include "fs/fs_proxy.h"
#include "fs/utils.h"
#include "utils/format.hpp"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::fs;

TEST_CASE("block iterator", "[model]") {
    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);

    auto path_guard = unique_path();
    auto root_view = path_guard.get_view(allocator);
    auto comparator = fs::get_comparator(root_view);

    auto interval = pt::microseconds{1};
    auto mediator = updates_mediator_t(interval, true);
    auto deadline = pt::microsec_clock::local_time() + interval;

    auto proxy = fs_proxy_t(mediator, deadline);
    SECTION("create_directories") {
        SECTION("single dir") {
            auto path = root_view / L"папка";
            auto ec = proxy.create_directories(path);
            CHECK(!ec);
#ifdef SYNCSPIRIT_WATCHER_KQUEUE
            CHECK(mediator.is_masked(root_view.get_full_name()) == 1);
#else
            CHECK(mediator.is_masked(path.get_full_name()) == 1);
#endif
            CHECK(proxy.mediator_updates == 1);
        }
    }

    SECTION("open_write") {
        auto path = root_view / L"папка" / L"файл.bin";

        create_directories(path.get_parent());

        SECTION("empty file") {
            auto f = proxy.open_write(path, 0);
            REQUIRE(f);
#ifdef SYNCSPIRIT_WATCHER_KQUEUE
            CHECK(mediator.is_masked(path.get_parent().get_full_name()) == 1);
#else
            CHECK(mediator.is_masked(path.get_full_name()) == 1);
#endif
            CHECK(proxy.mediator_updates == 1);
            REQUIRE(exists(path));
            CHECK(file_size(path) == 0);
        }
        SECTION("non-empty file") {
            auto f = proxy.open_write(path, 10);
            REQUIRE(f);
            REQUIRE(f.assume_value().close());
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
            CHECK(mediator.is_masked(path.get_full_name()) == 2);
#else
            CHECK(mediator.is_masked(path.get_full_name()) == 1);
            CHECK(mediator.is_masked(path.get_parent().get_full_name()) == 1);
#endif
            CHECK(proxy.mediator_updates == 2);
            REQUIRE(exists(path));
            CHECK(file_size(path) == 10);
        }
    }

    SECTION("remove_file") {
        auto path = root_view / L"под-снос";
        write_file(path, "");
        auto ec = proxy.remove(path);
        CHECK(!ec);
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
        CHECK(mediator.is_masked(path.get_full_name()) == 1);
#else
        CHECK(mediator.is_masked(path.get_full_name()) == 0);
        CHECK(mediator.is_masked(path.get_parent().get_full_name()) == 1);
#endif
        CHECK(!exists(path));
    }

    SECTION("remove") {
        auto path = root_view / L"под-снос";
        SECTION("file") {
            write_file(path, "");
            auto ec = proxy.remove(path);
            CHECK(!ec);
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
            CHECK(mediator.is_masked(path.get_full_name()) == 1);
#else
            CHECK(mediator.is_masked(path.get_full_name()) == 0);
            CHECK(mediator.is_masked(path.get_parent().get_full_name()) == 1);
#endif
            CHECK(proxy.mediator_updates == 1);
        }
        SECTION("empty dir") {
            create_directories(path);
            auto ec = proxy.remove(path);
            CHECK(!ec);
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
            CHECK(mediator.is_masked(path.get_full_name()) == 1);
#else
            CHECK(mediator.is_masked(path.get_full_name()) == 0);
            CHECK(mediator.is_masked(path.get_parent().get_full_name()) == 1);
#endif
            CHECK(proxy.mediator_updates == 1);
        }
        SECTION("non-dir") {
            create_directories(path / "bla-bla");
            auto ec = proxy.remove(path);
            CHECK(!ec);
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
            CHECK(mediator.is_masked(path.get_full_name()) == 1);
#else
            CHECK(mediator.is_masked(path.get_full_name()) == 0);
            CHECK(mediator.is_masked(path.get_parent().get_full_name()) == 1);
#endif
            CHECK(proxy.mediator_updates == 1);
        }
        CHECK(!exists(path));
    }
    SECTION("last_write_time") {
        auto path = root_view / L"файлик";
        auto ts = utils::timestamp_t{1234567890, 0};
        write_file(path, {});
        auto ec = proxy.last_write_time(path, ts.seconds);
        CHECK(!ec);
        CHECK(mediator.is_masked(path.get_full_name()) == 1);
        CHECK(proxy.mediator_updates == 1);
        CAPTURE(last_write_time(path));
        CHECK(comparator.equal(ts, last_write_time(path)));
        CHECK(comparator.equal(ts, last_write_time(path)));
    }
    if (comparator.permissions_supported) {
        SECTION("set_perms") {
            auto path = root_view / L"файлик";
            write_file(path, {});
            auto ec = proxy.set_perms(path, 0666);
            CHECK(!ec);
            CHECK(mediator.is_masked(path.get_full_name()) == 1);
            CHECK(proxy.mediator_updates == 1);
            CHECK(permissions(path) == 0666);
        }
    }
    if (comparator.symlinks_supported) {
        SECTION("create_link") {
            auto path = root_view / L"файлик";
            auto target = root_view / L"bla-bla";
            auto ec = proxy.create_link(target, path);
            CHECK(!ec);
            CHECK(mediator.is_masked(path.get_full_name()) == 1);
            CHECK(proxy.mediator_updates == 1);
            CHECK(read_symlink(path) == target.get_full_name());
        }
    }
    SECTION("rename") {
        auto path_1 = root_view / L"файлик1";
        auto path_2 = root_view / L"файлик2";
        write_file(path_1, {});
        auto ec = proxy.rename(path_1, path_2);
        CHECK(!ec);
        CHECK(exists(path_2));
        CHECK(mediator.is_masked(path_1.get_full_name()) == 1);
        CHECK(mediator.is_masked(path_2.get_full_name()) == 1);
        CHECK(proxy.mediator_updates == 1);
    }
    SECTION("write") {
        auto path = root_view / L"файл.bin";
        auto opt = proxy.open_write(path, 5);
        REQUIRE(opt);
        auto &f = opt.assume_value();
        mediator.clean_expired();
        CHECK(mediator.is_masked(path.get_full_name()) == 0);
        proxy.write(path, f, as_bytes("12345"));
        CHECK(mediator.is_masked(path.get_full_name()) == 1);
    }
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
