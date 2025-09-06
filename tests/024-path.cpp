// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
#include "model/misc/path.h"

using namespace syncspirit;
using namespace syncspirit::model;

TEST_CASE("path", "[presentation]") {
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

static bool _init = []() -> bool {
    test::init_logging();
    return true;
}();
