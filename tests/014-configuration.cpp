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
    auto cfg_opt = config::get_config(out);
    CHECK(cfg_opt);

    auto cfg2 = cfg_opt.value();
    CHECK(cfg == cfg2);
    fs::remove_all(dir);
}

