// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include "model/folder.h"
#include "test-utils.h"

using namespace syncspirit;
using namespace syncspirit::utils;
using namespace syncspirit::model;
using namespace syncspirit::test;

using Catch::Matchers::Matches;

TEST_CASE("folder file matchers", "[model]") {
    auto uuid = bu::uuid{};
    auto db = db::Folder();
    db::set_path(db, "/some/path");

    SECTION("default") {
        auto folder_opt = folder_t::create(uuid, db);
        REQUIRE(folder_opt.has_value());
        auto &f = folder_opt.value();
        CHECK(!f->accept("a/b/c"));
    }

    auto add_regex = [&](std::string_view pattern, file_match_t mode) {
        auto db_fm = db::FileMatcher();
        db::set_pattern(db_fm, std::string(pattern));
        db::set_mode(db_fm, mode);
        db::add_file_matcher(db, db_fm);
    };

    SECTION("serialize and deserialize") {
        CHECK(db::get_file_matcher_size(db) == 0);

        add_regex("a", file_match_t::ignore);
        add_regex("b", file_match_t::ignore);
        CHECK(db::get_file_matcher_size(db) == 2);

        auto bytes = db::encode(db);
        auto copy = db::Folder();
        auto left = db::decode(bytes, copy);
        CHECK(left == 0);
        CHECK(db::get_file_matcher_size(copy) == 2);
    }

    SECTION("ignore all") {
        add_regex(".*", file_match_t::ignore);
        auto f = folder_t::create(uuid, db).value();
        CHECK(!f->accept("a/b/c"));
    }

    SECTION("regex is off") {
        add_regex(".*", file_match_t::off);
        auto f = folder_t::create(uuid, db).value();
        CHECK(!f->accept("a/b/c"));
    }

    SECTION("accept all") {
        add_regex(".*", file_match_t::accept);
        auto f = folder_t::create(uuid, db).value();
        CHECK(f->accept(("a/b/c")));
    }

    SECTION("error in regex") {
        add_regex("\\", file_match_t::accept);
        auto f = folder_t::create(uuid, db).value();
        CHECK(!f->accept(("a")));
        CHECK(!f->accept("xxx"));
    }

    SECTION("accept some") {
        add_regex(".*aaa.*", file_match_t::accept);
        auto f = folder_t::create(uuid, db).value();
        CHECK(f->accept("aaaa/b/c"));
        CHECK(f->accept(("x/aaa/b/c")));
        CHECK(f->accept("aaa"));
        CHECK(!f->accept("bbb"));
        CHECK(!f->accept("ccc"));
    }

    SECTION("several rules") {
        add_regex(".*aaa.*", file_match_t::accept);
        add_regex(".*a.*", file_match_t::ignore);
        add_regex(".*", file_match_t::accept);
        auto f = folder_t::create(uuid, db).value();
        CHECK(f->accept("aaaa/b/c"));
        CHECK(f->accept("x/aaa/b/c"));
        CHECK(f->accept("aaa"));
        CHECK(!f->accept("aa"));
        CHECK(!f->accept("a"));
        CHECK(f->accept("bbb"));
        CHECK(f->accept("ccc"));
    }
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
