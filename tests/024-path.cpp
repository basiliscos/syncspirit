// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
#include "model/misc/path.h"
#include "model/misc/path_cache.h"

using namespace syncspirit;
using namespace syncspirit::model;

TEST_CASE("path", "[model]") {
    using pieces_t = std::vector<std::string_view>;
    SECTION("a/bb/c.txt") {
        auto p = path_t("a/bb/c.txt");
        CHECK(p.get_parent_name() == "a/bb");
        CHECK(p.get_own_name() == "c.txt");

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
        CHECK(p.get_own_name() == "file.bin");

        auto pieces = pieces_t();
        for (auto p : p) {
            pieces.emplace_back(p);
        }

        CHECK(pieces.size() == 2);
        CHECK(pieces[0] == "dir");
        CHECK(pieces[1] == "file.bin");
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
