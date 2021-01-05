#include "catch.hpp"
#include "test-utils.h"
#include "configuration.h"
#include "utils/uri.h"
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
    SECTION("serialize default") {
        auto r = config::serialize(cfg, out);
        CHECK(r);
        CHECK(out.str().find("~") == std::string::npos);
        auto cfg_opt = config::get_config(out, dir);
        CHECK(cfg_opt);

        auto cfg2 = cfg_opt.value();
        CHECK(cfg == cfg2);
        fs::remove_all(dir);
    }

    SECTION("ignored devices") {
        cfg.ingored_devices.emplace("O4LHPKG-O6BQ36W-MUOVKTI-MKAVHSC-Y7EC3U4-DHNLEDE-MZBJWQN-UIX6QAL");
        auto r = config::serialize(cfg, out);
        REQUIRE((bool)r);
        CHECK(out.str().find("O4LHPKG") != std::string::npos);
        //INFO(out.str());
        auto cfg2 = config::get_config(out, dir);
        CHECK(cfg2.value().ingored_devices.size() == 1);
        CHECK(cfg2.value() == cfg);
    }

    SECTION("devices & folders") {
        auto device = config::device_config_t{
            "O4LHPKG-O6BQ36W-MUOVKTI-MKAVHSC-Y7EC3U4-DHNLEDE-MZBJWQN-UIX6QAL",
            "my-device",
            config::compression_t::meta,
            "cert-issuer",
            true,
            true,
            false,
            true,
            { utils::parse("tcp://127.0.0.1:1234").value() },
        };
        cfg.devices.emplace(device.id, device);

        /*
        SECTION("device save & load") {
            auto r = config::serialize(cfg, out);
            REQUIRE((bool)r);
            CHECK(out.str().find("O4LHPKG") != std::string::npos);
            INFO(out.str());
            auto cfg2 = config::get_config(out, dir);
            CHECK(cfg2.value().devices.count(device.id) == 1);
            CHECK(cfg2.value() == cfg);
        }
        */

        SECTION("folders") {
            auto folder = config::folder_config_t {
                12345,
                "my-label",
                "/home/user/shared-folder",
                { device.id },
                config::folder_type_t::send_and_receive,
                3600,
                config::pull_order_t::alphabetic,
                true,
                true
            };
            cfg.folders.emplace(folder.id, folder);
            auto r = config::serialize(cfg, out);
            REQUIRE((bool)r);
            INFO(out.str());
            auto cfg2 = config::get_config(out, dir);
            CHECK(cfg2.value().folders.count(folder.id) == 1);
            CHECK(cfg2.value() == cfg);
            INFO(out.str());
        }
    }
}

