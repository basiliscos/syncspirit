#pragma once
#include <spdlog/spdlog.h>
#include <vector>

namespace syncspirit::config {

struct log_config_t {
    using sinks_t = std::vector<std::string>;

    std::string name;
    spdlog::level::level_enum level;
    sinks_t sinks;
};

using log_configs_t = std::vector<log_config_t>;

} // namespace syncspirit::config
