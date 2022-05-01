// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2022 Ivan Baidakou

#pragma once

#include <string>

namespace syncspirit::config {

struct relay_config_t {
    bool enabled;
    std::string discovery_url;
    std::uint32_t rx_buff_size;
};

} // namespace syncspirit::config
