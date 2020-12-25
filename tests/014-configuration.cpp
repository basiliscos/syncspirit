#include "catch.hpp"
#include "test-utils.h"
#include <configuration.h>
#include <boost/filesystem.hpp>
#include <sstream>

namespace sys = boost::system;
namespace fs = boost::filesystem;

using namespace syncspirit;

TEST_CASE("default config is OK", "[config]") {
    auto dir = fs::temp_directory_path().append(fs::unique_path().c_str());
    auto cfg_path = dir.append("syncspirit.toml");
    auto cfg = config::generate_config(cfg_path);
    std::stringstream out;
    auto r = config::serialize(cfg, out);
    CHECK(r);
    CHECK(out.str().find("~") == std::string::npos);
    auto cfg_opt = config::get_config(out, dir);
    CHECK(cfg_opt);

    auto cfg2 = cfg_opt.value();
    CHECK(cfg == cfg2);
    fs::remove_all(dir);

    SECTION("ignored devices") {
        out.clear();
        cfg.ingored_devices.emplace("O4LHPKG-O6BQ36W-MUOVKTI-MKAVHSC-Y7EC3U4-DHNLEDE-MZBJWQN-UIX6QAL");
        auto r = config::serialize(cfg, out);
        REQUIRE((bool)r);
        CHECK(out.str().find("O4LHPKG") != std::string::npos);
        INFO(out.str());
        auto cfg2 = config::get_config(out, dir);
        auto d = cfg2.value().ingored_devices.size();
        CHECK(d == 1);
        CHECK(cfg2.value() == cfg);
    }
}

