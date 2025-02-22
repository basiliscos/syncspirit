// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once
#include <cstdint>

namespace syncspirit::config {

struct upnp_config_t {
    bool enabled;
    bool debug;
    std::uint32_t max_wait;
    std::uint16_t external_port;
    std::uint32_t rx_buff_size;
};

} // namespace syncspirit::config
