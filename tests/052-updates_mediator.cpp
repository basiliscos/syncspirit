// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "test-utils.h"
#include "fs/updates_mediator.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::fs;

TEST_CASE("block iterator", "[model]") {
    auto interval = pt::microseconds{1};
    auto mediator = updates_mediator_t(interval);
    auto deadline_1 = pt::microsec_clock::local_time() + interval;
    auto deadline_2 = pt::microsec_clock::local_time() + interval * 2;
    auto deadline_3 = pt::microsec_clock::local_time() + interval * 3;

    SECTION("non-masked file") { CHECK(!mediator.is_masked("/tmp/path_1")); }
    SECTION("1 file, successful unmask") {
        mediator.push("/tmp/path_1", {}, deadline_1);
        CHECK(mediator.is_masked("/tmp/path_1"));
    }
    SECTION("1 file, double updates") {
        mediator.push("/tmp/path_1", {}, deadline_1);
        mediator.push("/tmp/path_1", {}, deadline_1);
        CHECK(mediator.is_masked("/tmp/path_1"));
        CHECK(!mediator.clean_expired());
    }
    SECTION("2 files, successful unmask") {
        mediator.push("/tmp/path_1", {}, deadline_1);
        mediator.push("/tmp/path_2", {}, deadline_1);
        CHECK(mediator.is_masked("/tmp/path_1"));
        CHECK(mediator.is_masked("/tmp/path_2"));
        CHECK(!mediator.clean_expired());
    }
    SECTION("1 file, postponed update") {
        mediator.push("/tmp/path_1", {}, deadline_1);
        mediator.push("/tmp/path_1", {}, deadline_2);
        CHECK(mediator.is_masked("/tmp/path_1"));
        CHECK(mediator.clean_expired());
        CHECK(mediator.is_masked("/tmp/path_1"));
    }
    SECTION("multiple files") {
        CHECK(!mediator.is_masked("/tmp/path_0"));
        mediator.push("/tmp/path_1", {}, deadline_1);
        mediator.push("/tmp/path_1", {}, deadline_1);
        mediator.push("/tmp/path_2", {}, deadline_1);
        mediator.push("/tmp/path_2", {}, deadline_2);
        mediator.push("/tmp/path_3", {}, deadline_2);

        CHECK(mediator.is_masked("/tmp/path_1"));
        CHECK(mediator.is_masked("/tmp/path_1"));
        CHECK(mediator.is_masked("/tmp/path_2"));
        CHECK(mediator.is_masked("/tmp/path_3"));

        CHECK(mediator.clean_expired());
        mediator.push("/tmp/path_4", {}, deadline_3);

        CHECK(!mediator.is_masked("/tmp/path_1"));
        CHECK(mediator.is_masked("/tmp/path_2"));
        CHECK(!mediator.is_masked("/tmp/path_3"));

        CHECK(mediator.clean_expired());
        CHECK(!mediator.is_masked("/tmp/path_1"));
        CHECK(!mediator.is_masked("/tmp/path_2"));
        CHECK(!mediator.is_masked("/tmp/path_3"));
        CHECK(mediator.is_masked("/tmp/path_4"));
        CHECK(!mediator.is_masked("/tmp/path_4"));

        CHECK(!mediator.clean_expired());
    }
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
