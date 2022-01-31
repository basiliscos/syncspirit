#pragma once
#include <cstdint>

namespace syncspirit::config {

struct fs_config_t {
    std::uint32_t batch_dirs_count; /* remove */
    std::uint32_t temporally_timeout;
    std::uint32_t mru_size;
};

} // namespace syncspirit::config
