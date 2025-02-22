// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <cstdint>

namespace syncspirit::config {

struct bep_config_t {
    std::uint32_t rx_buff_size;
    std::uint32_t tx_buff_limit;
    std::uint32_t connect_timeout;
    std::uint32_t request_timeout;
    std::uint32_t tx_timeout;
    std::uint32_t rx_timeout;
    std::uint32_t blocks_max_requested;
    std::uint32_t blocks_simultaneous_write;
    std::uint32_t advances_per_iteration;
};

} // namespace syncspirit::config
