#pragma once
#include <cstdint>

namespace syncspirit::config {

struct upnp_config_t {
    std::uint32_t discovery_attempts;
    std::uint32_t max_wait;
    std::uint32_t timeout;
    std::uint16_t external_port;
    std::uint32_t rx_buff_size;
};

} // namespace syncspirit::config