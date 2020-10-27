#pragma once

#include "utils/uri.h"
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <cstdint>
#include <fstream>
#include <iostream>

namespace syncspirit::config {

struct local_announce_config_t {
    std::uint16_t port;
    bool enabled;
};

struct global_announce_config_t {
    syncspirit::utils::URI announce_url;
    std::string device_id;
    std::string cert_file;
    std::string key_file;
    std::uint32_t rx_buff_size;
    std::uint32_t timeout;
    std::uint32_t reannounce_after;
};

struct upnp_config_t {
    std::uint32_t rx_buff_size;
    std::uint32_t max_wait;
    std::uint32_t timeout;
    std::uint16_t external_port;
};

struct configuration_t {
    local_announce_config_t local_announce_config;
    upnp_config_t upnp_config;
    global_announce_config_t global_announce_config;
    std::uint32_t timeout;
    std::string device_name;
};

boost::optional<configuration_t> get_config(std::ifstream &config);

} // namespace syncspirit::config
