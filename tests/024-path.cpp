// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "test-utils.h"
#include "model/misc/path.h"
#include "model/misc/path_view.hpp"
#include "model/misc/path_cache.h"
#include <memory_resource>

using namespace syncspirit;
using namespace syncspirit::model;

using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;

TEST_CASE("path", "[model]") {
    using pieces_t = std::vector<std::string_view>;
    SECTION("a/bb/c.txt") {
        auto p = path_t("a/bb/c.txt");
        SECTION("cloing") {
            auto p2 = path_t("a/bb/c.txt");
            CHECK(p == p2);
            auto p4 = std::move(p2);
            CHECK(p4 == p);
            CHECK(p2.empty());
        }

        CHECK(p.get_parent_name() == "a/bb");
        CHECK(p.get_filename() == "c.txt");

        auto pieces = pieces_t();
        for (auto p : p) {
            pieces.emplace_back(p);
        }

        CHECK(pieces.size() == 3);
        CHECK(pieces[0] == "a");
        CHECK(pieces[1] == "bb");
        CHECK(pieces[2] == "c.txt");
        CHECK(p.contains(p));
        CHECK(!p.contains(path_t("a/bb/c.tx")));
        CHECK(!p.contains(path_t("a/bb/c.x")));
        CHECK(!p.contains(path_t("a/bb/c")));
        CHECK(!p.contains(path_t("a/bb")));
        CHECK(!p.contains(path_t("a")));
        CHECK(path_t("a").contains(p));
        CHECK(path_t("a/").contains(p));
        CHECK(path_t("a/b").contains(p));
        CHECK(path_t("a/bb/c").contains(p));
    }
    SECTION("dir/file.bin") {
        auto p = path_t("dir/file.bin");
        CHECK(p.get_parent_name() == "dir");
        CHECK(p.get_filename() == "file.bin");

        auto pieces = pieces_t();
        for (auto p : p) {
            pieces.emplace_back(p);
        }

        CHECK(pieces.size() == 2);
        CHECK(pieces[0] == "dir");
        CHECK(pieces[1] == "file.bin");
    }
    SECTION("single") {
        auto p = path_t("file.bin");
        CHECK(p.get_filename() == "file.bin");
        CHECK(p.get_full_name() == "file.bin");
        CHECK(p.get_parent_name() == "");
    }
    SECTION("root") {
        auto p = path_t("/");
        CHECK(p.get_filename() == "");
        CHECK(p.get_full_name() == "/");
        CHECK(p.get_parent_name() == "");
    }
    SECTION("backslashes") {
        auto p = path_t("c:\\my\\path.bin");
        CHECK(p.get_filename() == "path.bin");
        CHECK(p.get_full_name() == "c:/my/path.bin");
        CHECK(p.get_parent_name() == "c:/my");
    }
}

TEST_CASE("path view (1)", "[model]") {
    auto allocator = std::allocator<char>();
    SECTION("abs path") {
        auto path = path_t("/some/dir/file.bin");
        auto view = path.get_view(allocator);
        CHECK(path == view);
        CHECK(view.get_filename() == "file.bin");
        CHECK(view.get_parent_name() == "/some/dir");
        SECTION("parent") {
            auto p1 = view.get_parent();
            CHECK(p1.get_full_name() == "/some/dir");
            CHECK(p1.get_filename() == "dir");

            auto p2 = p1.get_parent();
            CHECK(p2.get_full_name() == "/some");
            CHECK(p2.get_filename() == "some");

            auto p3 = p2.get_parent();
            CHECK(p3.get_full_name() == "/");
            CHECK(p3.get_filename() == "");

            CHECK(p3.get_parent().empty());
        }
    }
    SECTION("dir path") {
        auto path = path_t("/some/dir/");
        auto view = path.get_view(allocator);
        CHECK(path == view);
        CHECK(view.get_filename() == "");
        CHECK(view.get_parent_name() == "/some/dir");
        SECTION("parent") {
            auto p1 = view.get_parent();
            CHECK(p1.get_full_name() == "/some/dir");
            CHECK(p1.get_filename() == "dir");

            auto p2 = p1.get_parent();
            CHECK(p2.get_full_name() == "/some");
            CHECK(p2.get_filename() == "some");

            auto p3 = p2.get_parent();
            CHECK(p3.get_full_name() == "/");
            CHECK(p3.get_filename() == "");

            CHECK(p3.get_parent().empty());
        }
    }
    SECTION("rel path") {
        auto path = path_t("some/dir/file.bin");
        auto view = path.get_view(allocator);
        CHECK(path == view);
        CHECK(view.get_filename() == "file.bin");
        CHECK(view.get_parent_name() == "some/dir");
        SECTION("parent") {
            auto p1 = view.get_parent();
            CHECK(p1.get_full_name() == "some/dir");
            CHECK(p1.get_filename() == "dir");

            auto p2 = p1.get_parent();
            CHECK(p2.get_full_name() == "some");
            CHECK(p2.get_filename() == "some");

            auto p3 = p2.get_parent();
            CHECK(p3.empty());
            CHECK(p3.get_full_name() == "");
            CHECK(p3.get_filename() == "");
            CHECK(p3.get_parent().empty());
        }
    }

    SECTION("temporal") {
        auto path = path_t("/some/dir/file.bin");
        CHECK(!path.is_temporal());
        auto view = path.get_view(allocator).make_temporal();
        CHECK(view.get_full_name() == "/some/dir/file.bin.syncspirit-tmp");
        CHECK(view.is_temporal());
        CHECK(!view.get_parent().is_temporal());
        CHECK(view.get_parent().get_full_name() == "/some/dir");
    }

    SECTION("absolutness") {
#ifndef SYNCSPIRIT_WIN
        CHECK(path_t("/some/dir/file.bin").is_absolute());
        CHECK(!path_t("some/dir/file.bin").is_absolute());
#else
        CHECK(path_t("c:/some/dir/file.bin").is_absolute());
        CHECK(path_t("c:\\some\\dir\\file.bin").is_absolute());
        CHECK(!path_t("some/dir/file.bin").is_absolute());
        CHECK(!path_t("some\\dir\\file.bin").is_absolute());
#endif
    }
}

TEST_CASE("path view (2)", "[model]") {
    auto buffer = std::array<std::byte, 1024 * 128>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);

#ifndef SYNCSPIRIT_WIN
    auto p_abs_1 = std::string_view("/a/b");
    auto p_abs_2 = std::string_view("/c/d");
#else
    auto p_abs_1 = std::string_view("c:\\a\\b");
    auto p_abs_2 = std::string_view("c:\\c\\d");
#endif

    SECTION("concat") {
        SECTION("2 relatives") {
            auto p1 = path_t("a/b").get_view(allocator);
            auto p2 = path_t("c/d").get_view(allocator);
            auto pr = p1 / p2;
            CHECK(pr.get_full_name() == "a/b/c/d");
            CHECK(!pr.is_absolute());

            auto pr_2 = pr.detach();
            CHECK(pr == pr_2);
        }
        SECTION("2 absolutes") {
            auto p1 = path_t(p_abs_1).get_view(allocator);
            auto p2 = path_t(p_abs_2).get_view(allocator);
            auto pr = p1 / p2;
            CHECK(pr == p2);
            CHECK(pr.get_full_name() == p2.get_full_name());
            CHECK(pr.is_absolute());
        }
        SECTION("rel + abs") {
            auto p1 = path_t("a/b").get_view(allocator);
            auto p2 = path_t(p_abs_2).get_view(allocator);
            auto pr = p1 / p2;
            CHECK(pr == p2);
            CHECK(pr.get_full_name() == p2.get_full_name());
            CHECK(pr.is_absolute());
        }
        SECTION("abs + rel") {
            auto p1 = path_t(p_abs_1).get_view(allocator);
            auto p2 = path_t("c/d").get_view(allocator);
            auto pr = p1 / p2;
            CHECK(pr.is_absolute());
            auto full = std::string(pr.get_full_name());
            REQUIRE_THAT(full, StartsWith(std::string(p1.get_full_name())));
            REQUIRE_THAT(full, EndsWith(std::string(p2.get_full_name())));
        }
    }
}

TEST_CASE("path_cache", "[model]") {
    auto cache = path_cache_ptr_t(new path_cache_t());
    auto path = cache->get_path("a/b/c");
    REQUIRE(path);
    CHECK(path->use_count() == 1);
    CHECK(cache->map.size() == 1);

    auto path_2 = cache->get_path("a/b/c");
    REQUIRE(path_2);
    CHECK(path_2 == path);
    CHECK(path->use_count() == 2);
    CHECK(cache->map.size() == 1);

    auto path_3 = cache->get_path("x/y/z");
    REQUIRE(path_3);
    CHECK(path_3->use_count() == 1);
    CHECK(cache->map.size() == 2);

    path_3.reset();
    CHECK(cache->map.size() == 1);

    path_2.reset();
    CHECK(cache->map.size() == 1);
    CHECK(path->use_count() == 1);

    path.reset();
    CHECK(cache->map.size() == 0);
}

static bool _init = []() -> bool {
    test::init_logging();
    return true;
}();
