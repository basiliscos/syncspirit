// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "utils/log.h"
#include "utils/log-setup.h"
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace st = syncspirit::test;
namespace bfs = std::filesystem;

using namespace syncspirit;

using L = spdlog::level::level_enum;

static bool _init = []() {
    auto dist_sink = utils::create_root_logger();
    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    dist_sink->add_sink(console_sink);
    dist_sink->add_sink(std::make_shared<spdlog::sinks::null_sink_mt>());
    return true;
}();

TEST_CASE("default logger", "[log]") {
    config::log_configs_t cfg{{"default", L::trace, {"stdout"}}};
    REQUIRE(utils::init_loggers(cfg));
    auto l = utils::get_logger("default");
    CHECK(l);
    CHECK(l->level() == L::trace);
}

TEST_CASE("hierarchy", "[log]") {
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
    auto dir = bfs::absolute(bfs::current_path() / st::unique_path());
    auto path_guard = st::path_guard_t{dir};
    bfs::create_directory(dir);
    auto log_file = dir / "log.txt";
    auto log_file_str = log_file.string();

    auto sink_config = fmt::format("file:{}", log_file_str);
    config::log_configs_t cfg{{"default", L::trace, {sink_config}}};
    REQUIRE(utils::init_loggers(cfg));
    auto l = utils::get_logger("default");
    l->info("lorem ipsum dolor");
    l->flush();

    spdlog::drop_all(); // to cleanup on win32
    auto data = st::read_file(log_file);
    CHECK(log_file_str != "");
    CHECK(!data.empty());
    CHECK(data.find("lorem ipsum dolor") != std::string::npos);
}
