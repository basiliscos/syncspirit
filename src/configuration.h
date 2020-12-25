#pragma once

#include "utils/uri.h"
#include <boost/outcome.hpp>
#include <boost/filesystem.hpp>
#include <cstdint>
#include <istream>
#include <ostream>
#include <vector>
#include <set>
#include "model/device.h"

namespace syncspirit::config {

namespace outcome = boost::outcome_v2;

struct tui_config_t {
    std::uint32_t refresh_interval;
    char key_quit;
    char key_more_logs;
    char key_less_logs;
    char key_help;
    char key_config;

    inline bool operator==(const tui_config_t &other) const noexcept {
        return refresh_interval == other.refresh_interval && key_quit == other.key_quit &&
               key_more_logs == other.key_more_logs && key_less_logs == other.key_less_logs &&
               key_help == other.key_help && key_config == other.key_config;
    }
};

struct local_announce_config_t {
    bool enabled;
    std::uint16_t port;
    std::uint32_t frequency;
    inline bool operator==(const local_announce_config_t &other) const noexcept {
        return enabled == other.enabled && port == other.port && frequency == other.frequency;
    }
};

struct global_announce_config_t {
    bool enabled;
    syncspirit::utils::URI announce_url;
    std::string device_id;
    std::string cert_file;
    std::string key_file;
    std::uint32_t rx_buff_size;
    std::uint32_t timeout;
    std::uint32_t reannounce_after = 600;
    inline bool operator==(const global_announce_config_t &other) const noexcept {
        return enabled == other.enabled && announce_url == other.announce_url && device_id == other.device_id &&
               cert_file == other.cert_file && key_file == other.key_file && rx_buff_size == other.rx_buff_size &&
               timeout == other.timeout && reannounce_after == other.reannounce_after;
    }
};

struct upnp_config_t {
    std::uint32_t discovery_attempts;
    std::uint32_t max_wait;
    std::uint32_t timeout;
    std::uint16_t external_port;
    std::uint32_t rx_buff_size;
    inline bool operator==(const upnp_config_t &other) const noexcept {
        return discovery_attempts == other.discovery_attempts && max_wait == other.max_wait &&
               timeout == other.timeout && external_port == other.external_port && rx_buff_size == other.rx_buff_size;
    }
};

struct bep_config_t {
    std::uint32_t rx_buff_size;
    inline bool operator==(const bep_config_t &other) const noexcept { return rx_buff_size == other.rx_buff_size; }
};

struct configuration_t {
    using ingored_devices_t = std::set<std::string>;

    boost::filesystem::path config_path;
    local_announce_config_t local_announce_config;
    upnp_config_t upnp_config;
    global_announce_config_t global_announce_config;
    bep_config_t bep_config;
    tui_config_t tui_config;

    std::uint32_t timeout;
    std::string device_name;
    ingored_devices_t ingored_devices;

    inline bool operator==(const configuration_t &other) const noexcept {
        return local_announce_config == other.local_announce_config && upnp_config == other.upnp_config &&
               global_announce_config == other.global_announce_config && bep_config == other.bep_config &&
               tui_config == other.tui_config && timeout == other.timeout && device_name == other.device_name &&
               config_path == other.config_path && ingored_devices == other.ingored_devices;
    }
};

using config_result_t = outcome::outcome<configuration_t, std::string>;

config_result_t get_config(std::istream &config, const boost::filesystem::path &config_path);

configuration_t generate_config(const boost::filesystem::path &config_path);

outcome::result<void> serialize(const configuration_t cfg, std::ostream &out) noexcept;

} // namespace syncspirit::config
