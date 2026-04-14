// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include "test-utils.h"
#include "proto/proto-helpers-db.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::proto;

TEST_CASE("folder::set_path normalization", "[db]") {
    auto value = db::Folder();
    auto path = GENERATE("/a/b", "/a/b/", "/a/b//", "/a/b///", "/a/b\\", "/a/b\\\\");
    db::set_path(value, path);
    CHECK(db::get_path(value) == "/a/b");
}
