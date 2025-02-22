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
    constexpr std::int64_t kb = 1024;
    constexpr std::int64_t mb = kb * kb;
    constexpr std::int64_t gb = kb * kb * kb;

    SECTION("no prev_size") {
        CHECK(get_block_size(1, 0) == D{1, 1});
        CHECK(get_block_size(2, 0) == D{1, 2});
        CHECK(get_block_size(128 * kb, 0) == D{1, 128 * kb});
        CHECK(get_block_size(128 * kb + 1, 0) == D{2, 128 * kb});

        SECTION("gb") {
            CHECK(gb == 1073741824ull);
            auto d1 = get_block_size(16 * gb, 0);
            auto d2 = D{1024, 16 * mb};
            auto sz = 16 * gb;
            CHECK(d1.size == d2.size);
            CHECK(d1.count == d2.count);
            CHECK(get_block_size(sz, 0) == d2);
        }
    }

    SECTION("with prev_size") {
        CHECK(get_block_size(1, 256 * kb) == D{1, 1});
        CHECK(get_block_size(1 * gb, 256 * kb) == D{4096, 256 * kb});
    };
}
