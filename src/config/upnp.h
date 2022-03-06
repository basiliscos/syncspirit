#pragma once
#include <cstdint>

namespace syncspirit::config {

struct upnp_config_t {
    bool enabled;
    std::uint32_t max_wait;
    std::uint16_t external_port;
    std::uint32_t rx_buff_size;
};

} // namespace syncspirit::config
