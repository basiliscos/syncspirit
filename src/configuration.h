#pragma once

#include "utils/uri.h"
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>

namespace syncspirit::config {

struct console_sink_t {
    spdlog::level::level_enum level;
};

struct file_sink_t {
    spdlog::level::level_enum level;
    std::string file;
};

using sink_config_t = boost::variant<console_sink_t, file_sink_t>;

struct logging_config_t {
    std::vector<sink_config_t> sinks;
};

struct local_announce_config_t {
    std::uint16_t port;
    bool enabled;
};

struct global_announce_config_t {
    syncspirit::utils::URI server_url;
    std::string cert_file;
    std::string key_file;
    std::uint32_t timeout;
};

struct upnp_config_t {
    std::uint32_t rx_buff_size;
    std::uint32_t max_wait;
    std::uint32_t timeout;
};

struct configuration_t {
    logging_config_t logging_config;
    local_announce_config_t local_announce_config;
    upnp_config_t upnp_config;
    global_announce_config_t global_announce_config;
};

spdlog::level::level_enum get_log_level(const std::string &log_level);

boost::optional<configuration_t> get_config(std::ifstream &config);

} // namespace syncspirit::config
