// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "utils.h"
#include <cstdint>

namespace syncspirit::fs {

const std::string_view tmp_suffix = ".syncspirit-tmp";

static const std::int32_t _block_sizes[] = {
    // clang-format off
    (1 << 7) * 1024ull,
    (1 << 8) * 1024ull,
    (1 << 9) * 1024ull,
    (1 << 10) * 1024ull,
    (1 << 11) * 1024ull,
    (1 << 12) * 1024ull,
    (1 << 13) * 1024ull,
    (1 << 14) * 1024ull,
    // clang-format on
};

const std::size_t block_sizes_sz = 8;
const std::int32_t *block_sizes = _block_sizes;

static const constexpr size_t max_blocks_count = 2000;

block_division_t get_block_size(int64_t sz, int32_t prev_size) noexcept {
    auto bs = std::int64_t{0};
    if (block_sizes[0] <= sz) {
        for (size_t i = 0; i < block_sizes_sz; ++i) {
            if (block_sizes[i] == prev_size) {
                bs = prev_size;
                break;
            }
        }
    }

    if (!bs) {
        for (size_t i = 0; i < block_sizes_sz; ++i) {
            if (block_sizes[i] * static_cast<std::int64_t>(max_blocks_count) >= sz) {
                bs = block_sizes[i];
                if (bs > sz) {
                    bs = sz;
                }
                break;
            }
        }
    }
    if (bs == 0 && sz) {
        bs = block_sizes[block_sizes_sz - 1];
    }

    auto count = std::int32_t{0};
    if (bs != 0) {
        count = sz / bs;
        if (count * bs < sz) {
            ++count;
        }
    }

    return {count, (int32_t)bs};
}

} // namespace syncspirit::fs
