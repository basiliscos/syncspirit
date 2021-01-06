#pragma once
#include <cstdint>

namespace syncspirit::config {

struct tui_config_t {
    std::uint32_t refresh_interval;
    char key_quit;
    char key_more_logs;
    char key_less_logs;
    char key_help;
    char key_config;
};

} // namespace syncspirit::config
