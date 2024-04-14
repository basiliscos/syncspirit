// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#pragma once
#include <cstdint>

namespace syncspirit::config {

struct db_config_t {
    std::int64_t upper_limit;
    std::uint32_t uncommitted_threshold;
};

} // namespace syncspirit::config
