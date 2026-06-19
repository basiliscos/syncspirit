// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "test-utils.h"
#include "utils/log.h"
#include "utils/format.hpp"
#include "utils/log-setup.h"
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <utils/path_view.hpp>

namespace st = syncspirit::test;

using namespace syncspirit;

using L = spdlog::level::level_enum;

static auto init_root = []() {
    auto [dist_sink, _] = utils::create_root_logger();
    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    dist_sink->add_sink(console_sink);
    dist_sink->add_sink(std::make_shared<spdlog::sinks::null_sink_mt>());
};

static bool _init = []() {
    init_root();
    return true;
}();

TEST_CASE("default logger", "[log]") {
    init_root();
    config::log_configs_t cfg{{"default", L::trace, {"stdout"}}};
    REQUIRE(utils::init_loggers(cfg));
    auto l = utils::get_logger("default");
    CHECK(l);
    CHECK(l->level() == L::trace);
}

TEST_CASE("hierarchy", "[log]") {
    init_root();
    config::log_configs_t cfg{{"default", L::trace, {"stdout"}}, {"a", L::info, {}}, {"a.b.c", L::warn, {}}};
    REQUIRE(utils::init_loggers(cfg));
    SECTION("custom") {
        auto l = utils::get_logger("a");
        REQUIRE(l);
        CHECK(l->level() == L::info);
    }
    SECTION("submatch") {
        auto l = utils::get_logger("a.b");
        REQUIRE(l);
        CHECK(l->level() == L::info);
    }
    SECTION("full match") {
        auto l = utils::get_logger("a.b.c");
        REQUIRE(l);
        CHECK(l->level() == L::warn);
    }
    SECTION("mismatch") {
        auto l = utils::get_logger("xxx");
        REQUIRE(l);
        CHECK(l->level() == L::trace);
    }
}

TEST_CASE("file sink", "[log]") {
    init_root();
    auto path_guard = st::path_guard_t();
    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);

    auto log_file = path_guard.get_view(allocator) / L"папка" / L"журнал.txt";
    auto sink_config = fmt::format("file:{}", log_file);
    INFO("log_file = " << sink_config);

    config::log_configs_t cfg{{"default", L::trace, {sink_config}}};
    REQUIRE(utils::init_loggers(cfg));
    auto l = utils::get_logger("default");
    l->info("lorem ipsum dolor");
    l->flush();

    utils::finalize_loggers(); // to cleanup on win32
    auto data = st::read_file(log_file);
    CHECK(!data.empty());
    CHECK(data.find("lorem ipsum dolor") != std::string::npos);
}
