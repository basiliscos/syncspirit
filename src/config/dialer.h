#pragma once

#include <cstdint>

namespace syncspirit::config {

struct dialer_config_t {
    bool enabled;
    std::uint32_t redial_timeout;
};

} // namespace syncspirit::config
