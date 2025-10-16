// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "config/utils.h"
#include "utils/uri.h"
#include "utils/location.h"
#include <filesystem>
#include <sstream>

namespace syncspirit::config {

bool operator==(const bep_config_t &lhs, const bep_config_t &rhs) noexcept {
    return lhs.rx_buff_size == rhs.rx_buff_size && lhs.tx_buff_limit == rhs.tx_buff_limit &&
           lhs.connect_timeout == rhs.connect_timeout && lhs.ping_timeout == rhs.ping_timeout &&
           lhs.blocks_max_requested == rhs.blocks_max_requested &&
           lhs.blocks_simultaneous_write == rhs.blocks_simultaneous_write;
}

bool operator==(const dialer_config_t &lhs, const dialer_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.redial_timeout == rhs.redial_timeout &&
           lhs.skip_discovers == rhs.skip_discovers;
}

bool operator==(const fs_config_t &lhs, const fs_config_t &rhs) noexcept {
    return lhs.temporally_timeout == rhs.temporally_timeout && lhs.mru_size == rhs.mru_size &&
           lhs.bytes_scan_iteration_limit == rhs.bytes_scan_iteration_limit &&
           lhs.files_scan_iteration_limit == rhs.files_scan_iteration_limit;
}

bool operator==(const db_config_t &lhs, const db_config_t &rhs) noexcept {
    return lhs.upper_limit == rhs.upper_limit && lhs.uncommitted_threshold == rhs.uncommitted_threshold &&
           lhs.max_blocks_per_diff == rhs.max_blocks_per_diff && lhs.max_files_per_diff == rhs.max_files_per_diff;
}

bool operator==(const global_announce_config_t &lhs, const global_announce_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.debug == rhs.debug && *lhs.announce_url == *rhs.announce_url &&
           *lhs.lookup_url == *rhs.lookup_url && lhs.rx_buff_size == rhs.rx_buff_size && lhs.timeout == rhs.timeout &&
           lhs.reannounce_after == rhs.reannounce_after;
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
    return lhs.enabled == rhs.enabled && lhs.debug == rhs.debug && *lhs.discovery_url == *rhs.discovery_url &&
           lhs.rx_buff_size == rhs.rx_buff_size;
}

bool operator==(const main_t &lhs, const main_t &rhs) noexcept {
    return lhs.local_announce_config == rhs.local_announce_config && lhs.upnp_config == rhs.upnp_config &&
           lhs.global_announce_config == rhs.global_announce_config && lhs.bep_config == rhs.bep_config &&
           lhs.db_config == rhs.db_config && lhs.timeout == rhs.timeout && lhs.device_name == rhs.device_name &&
           lhs.config_path == rhs.config_path && lhs.log_configs == rhs.log_configs && lhs.cert_file == rhs.cert_file &&
           lhs.key_file == rhs.key_file && lhs.hasher_threads == rhs.hasher_threads;
}

} // namespace syncspirit::config

namespace sys = boost::system;
namespace fs = std::filesystem;
namespace st = syncspirit::test;

using namespace syncspirit;

TEST_CASE("expand_home", "[config]") {
    SECTION("valid home") {
        auto home = utils::home_option_t(fs::path("/user/home/.config/syncspirit_test"));
        REQUIRE(utils::expand_home("some/path", home) == L"some/path");
        REQUIRE(utils::expand_home("~/some/path", home) == L"/user/home/.config/syncspirit_test/some/path");
    }

    SECTION("invalid home") {
        auto ec = sys::error_code{1, sys::system_category()};
        auto home = utils::home_option_t(ec);
        REQUIRE(utils::expand_home("some/path", home) == L"some/path");
        REQUIRE(utils::expand_home("~/some/path", home) == L"~/some/path");
    }
}

TEST_CASE("default config is OK", "[config]") {
    auto dir = st::unique_path();
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
