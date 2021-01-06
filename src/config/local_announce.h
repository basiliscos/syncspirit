#pragma once
#include <cstdint>

namespace syncspirit::config {

struct local_announce_config_t {
    bool enabled;
    std::uint16_t port;
    std::uint32_t frequency;
};

} // namespace syncspirit::config
