#pragma once
#include <cstdint>

namespace syncspirit::config {

struct fs_config_t {
    std::uint32_t batch_block_size;
    std::uint32_t batch_dirs_count;
};

} // namespace syncspirit::config
