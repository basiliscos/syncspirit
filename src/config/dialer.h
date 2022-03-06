// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <cstdint>

namespace syncspirit::config {

struct dialer_config_t {
    bool enabled;
    std::uint32_t redial_timeout;
};

} // namespace syncspirit::config
