// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "catch.hpp"
#include "test-utils.h"
#include "config/utils.h"
#include "utils/uri.h"
#include <boost/filesystem.hpp>
#include <sstream>

namespace syncspirit::config {

bool operator==(const bep_config_t &lhs, const bep_config_t &rhs) noexcept {
    return lhs.rx_buff_size == rhs.rx_buff_size && lhs.connect_timeout == rhs.connect_timeout &&
           lhs.request_timeout == rhs.request_timeout && lhs.tx_timeout == rhs.tx_timeout &&
           lhs.rx_timeout == rhs.rx_timeout && lhs.blocks_max_requested == rhs.blocks_max_requested;
}

bool operator==(const dialer_config_t &lhs, const dialer_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.redial_timeout == rhs.redial_timeout;
}

bool operator==(const fs_config_t &lhs, const fs_config_t &rhs) noexcept {
    return lhs.temporally_timeout == rhs.temporally_timeout && lhs.mru_size == rhs.mru_size;
}

bool operator==(const db_config_t &lhs, const db_config_t &rhs) noexcept {
    return lhs.upper_limit == rhs.upper_limit && lhs.uncommited_threshold == rhs.uncommited_threshold;
}

bool operator==(const global_announce_config_t &lhs, const global_announce_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.announce_url == rhs.announce_url && lhs.device_id == rhs.device_id &&
           lhs.cert_file == rhs.cert_file && lhs.key_file == rhs.key_file && lhs.rx_buff_size == rhs.rx_buff_size &&
           lhs.timeout == rhs.timeout && lhs.reannounce_after == rhs.reannounce_after;
}

bool operator==(const local_announce_config_t &lhs, const local_announce_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.port == rhs.port && lhs.frequency == rhs.frequency;
}

bool operator==(const log_config_t &lhs, const log_config_t &rhs) noexcept {
    return lhs.name == rhs.name && lhs.level == rhs.level && lhs.sinks == rhs.sinks;
}

bool operator==(const upnp_config_t &lhs, const upnp_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.max_wait == rhs.max_wait && lhs.external_port == rhs.external_port &&
           lhs.rx_buff_size == rhs.rx_buff_size && lhs.debug == rhs.debug;
}

bool operator==(const relay_config_t &lhs, const relay_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.discovery_url == rhs.discovery_url;
}

bool operator==(const main_t &lhs, const main_t &rhs) noexcept {
    return lhs.local_announce_config == rhs.local_announce_config && lhs.upnp_config == rhs.upnp_config &&
           lhs.global_announce_config == rhs.global_announce_config && lhs.bep_config == rhs.bep_config &&
           lhs.db_config == rhs.db_config && lhs.timeout == rhs.timeout && lhs.device_name == rhs.device_name &&
           lhs.config_path == rhs.config_path && lhs.log_configs == rhs.log_configs &&
           lhs.hasher_threads == rhs.hasher_threads;
}

} // namespace syncspirit::config

namespace sys = boost::system;
namespace fs = boost::filesystem;
namespace st = syncspirit::test;

using namespace syncspirit;

TEST_CASE("default config is OK", "[config]") {
    auto dir = fs::current_path() / fs::unique_path();
    fs::create_directory(dir);
    auto dir_guard = st::path_guard_t(dir);
    auto cfg_path = dir / "syncspirit.toml";
    auto cfg_opt = config::generate_config(cfg_path);
    REQUIRE(cfg_opt);
    auto &cfg = cfg_opt.value();
    std::stringstream out;
    SECTION("serialize default") {
        auto r = config::serialize(cfg, out);
        CHECK(r);
        INFO(out.str());
        CHECK(out.str().find("~") == std::string::npos);
        auto cfg_opt = config::get_config(out, cfg_path);
        CHECK(cfg_opt);

        auto cfg2 = cfg_opt.value();
        CHECK(cfg == cfg2);
    }
}
