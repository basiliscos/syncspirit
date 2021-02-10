#pragma once
#include <cstdint>

namespace syncspirit::config {

struct bep_config_t {
    std::uint32_t rx_buff_size;
    std::uint32_t connect_timeout;
    std::uint32_t tx_timeout;
    std::uint32_t rx_timeout;
};

} // namespace syncspirit::config
