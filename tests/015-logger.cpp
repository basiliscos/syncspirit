#include "catch.hpp"
#include "test-utils.h"
#include "utils/log.h"

namespace st = syncspirit::test;
using namespace syncspirit;

using L = spdlog::level::level_enum;

std::string prompt;
std::mutex mutex;
bool overwrite = false;
bool interactive = false;


TEST_CASE("default logger", "[log]") {
    config::log_configs_t cfg {
        {"default", L::trace, {"stdout"}}
    };
    REQUIRE(utils::init_loggers(cfg, prompt, mutex, overwrite, interactive));
    auto l = utils::get_logger("default");
    CHECK(l);
    CHECK(l->level() == L::trace);
}

TEST_CASE("hierarcy", "[log]") {
    config::log_configs_t cfg {
        {"default", L::trace, {"stdout"}},
        {"a", L::info, {}},
        {"a.b.c", L::warn, {}}
    };
    REQUIRE(utils::init_loggers(cfg, prompt, mutex, overwrite, interactive));
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
