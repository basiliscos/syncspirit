// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include <boost/outcome.hpp>
#include <cstdint>
#include "syncspirit-export.h"

namespace syncspirit {
namespace fs {

namespace outcome = boost::outcome_v2;

struct block_division_t {
    std::int32_t count;
    std::int32_t size;
};

SYNCSPIRIT_API block_division_t get_block_size(int64_t file_size, int32_t prev_size) noexcept;

SYNCSPIRIT_API extern const std::size_t block_sizes_sz;
SYNCSPIRIT_API extern const std::int32_t *block_sizes;
SYNCSPIRIT_API extern const std::string_view tmp_suffix;

} // namespace fs
} // namespace syncspirit
