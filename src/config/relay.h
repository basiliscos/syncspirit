// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "utils/uri.h"

namespace syncspirit::config {

struct relay_config_t {
    bool enabled;
    bool debug;
    utils::uri_ptr_t discovery_url;
    std::uint32_t rx_buff_size;
};

} // namespace syncspirit::config
