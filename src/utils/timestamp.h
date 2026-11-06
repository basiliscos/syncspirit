// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#pragma once

#include <cstdint>
#include "syncspirit-export.h"

namespace syncspirit::utils {

struct SYNCSPIRIT_API timestamp_t {
    std::int64_t seconds;
    std::int32_t ns;
};

} // namespace syncspirit::utils