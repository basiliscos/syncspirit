// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023 Ivan Baidakou

#include "test-utils.h"
#include "fs/utils.h"

using namespace syncspirit::fs;

using D = block_division_t;

namespace syncspirit::fs {

bool operator==(const D &lhs, const D &rhs) noexcept { return lhs.count == rhs.count && lhs.size == rhs.size; }

} // namespace syncspirit::fs

TEST_CASE("get_block_size", "[fs]") {
    constexpr size_t kb = 1024;
    constexpr size_t mb = kb * kb;
    constexpr size_t gb = kb * kb * kb;

    SECTION("no prev_size") {
        CHECK(get_block_size(1, 0) == D{1, 1});
        CHECK(get_block_size(2, 0) == D{1, 2});
        CHECK(get_block_size(128 * kb, 0) == D{1, 128 * kb});
        CHECK(get_block_size(128 * kb + 1, 0) == D{2, 128 * kb});
        CHECK(get_block_size(16 * gb, 0) == D{1024, 16 * mb});
    }

    SECTION("with prev_size") {
        CHECK(get_block_size(1, 256 * kb) == D{1, 1});
        CHECK(get_block_size(1 * gb, 256 * kb) == D{4096, 256 * kb});
    };
}
