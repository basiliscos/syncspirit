#include "catch.hpp"
#include "test-utils.h"
#include "utils/log.h"

namespace st = syncspirit::test;
namespace bfs = boost::filesystem;

using namespace syncspirit;

using L = spdlog::level::level_enum;

bool overwrite = false;

TEST_CASE("default logger", "[log]") {
    config::log_configs_t cfg{{"default", L::trace, {"stdout"}}};
    REQUIRE(utils::init_loggers(cfg, overwrite));
    auto l = utils::get_logger("default");
    CHECK(l);
    CHECK(l->level() == L::trace);
}

TEST_CASE("hierarcy", "[log]") {
    config::log_configs_t cfg{{"default", L::trace, {"stdout"}}, {"a", L::info, {}}, {"a.b.c", L::warn, {}}};
    REQUIRE(utils::init_loggers(cfg, overwrite));
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
    auto dir = bfs::path{bfs::unique_path()};
    auto path_guard = st::path_guard_t{dir};
    bfs::create_directory(dir);

    auto file_path = fmt::format("{}/log.txt", dir.string());
    auto sink_config = fmt::format("file:{}", file_path);
    config::log_configs_t cfg{{"default", L::trace, {sink_config}}};
    REQUIRE(utils::init_loggers(cfg, overwrite));
    auto l = utils::get_logger("default");
    l->info("lorem ipsum dolor");
    l->flush();

    spdlog::drop_all(); // to cleanup on win32
    auto data = st::read_file(bfs::path(file_path));
    CHECK(!data.empty());
    CHECK(data.find("lorem ipsum dolor") != std::string::npos);
}
