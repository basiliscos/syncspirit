// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#pragma once

#include <cstdint>

namespace syncspirit::utils {

struct permissions_t {
    std::uint32_t value;
    bool absent;
};

} // namespace syncspirit::utils