// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#pragma once

#include <cstdint>

namespace syncspirit::utils {

struct timestamp_t {
    std::int64_t seconds;
    std::int32_t ns;
};

} // namespace syncspirit::utils