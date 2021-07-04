#include "catch.hpp"
#include "test-utils.h"
#include "config/utils.h"
#include "utils/uri.h"
#include <boost/filesystem.hpp>
#include <sstream>

namespace sys = boost::system;
namespace fs = boost::filesystem;

using namespace syncspirit;

TEST_CASE("default config is OK", "[config]") {
    auto dir = fs::current_path() / fs::unique_path();
    fs::create_directory(dir);
    auto cfg_path = dir / "syncspirit.toml";
    auto cfg_opt = config::generate_config(cfg_path);
    REQUIRE(cfg_opt);
    auto& cfg = cfg_opt.value();
    std::stringstream out;
    SECTION("serialize default") {
        auto r = config::serialize(cfg, out);
        CHECK(r);
        CHECK(out.str().find("~") == std::string::npos);
        auto cfg_opt = config::get_config(out, cfg_path);
        CHECK(cfg_opt);

        auto cfg2 = cfg_opt.value();
        CHECK(cfg == cfg2);
    }
    fs::remove_all(dir);
}
