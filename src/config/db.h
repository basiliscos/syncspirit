#pragma once

#pragma once
#include <cstdint>

namespace syncspirit::config {

struct db_config_t {
    std::int64_t upper_limit;
    std::uint32_t uncommited_threshold;
};

} // namespace syncspirit::config
