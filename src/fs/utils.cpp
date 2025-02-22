// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "utils.h"
#include <cstdint>

namespace syncspirit::fs {

const std::string_view tmp_suffix = ".syncspirit-tmp";

static const std::int64_t _block_sizes[] = {
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
const std::int64_t *block_sizes = _block_sizes;

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

    auto count = std::int64_t{0};
    if (bs != 0) {
        count = sz / bs;
        if (count * bs < sz) {
            ++count;
        }
    }

    return {static_cast<size_t>(count), (int32_t)bs};
}

bfs::path make_temporal(const bfs::path &path) noexcept {
    auto copy = path;
    copy += tmp_suffix.data();
    return copy;
}

bool is_temporal(const bfs::path &path) noexcept {
    auto name = path.string();
    if (name.length() < tmp_suffix.length()) {
        return false;
    }
    auto pos = name.find(tmp_suffix, name.length() - tmp_suffix.length());
    return (pos != name.npos);
}

bfs::path relativize(const bfs::path &path, const bfs::path &root) noexcept {
    auto it_path = path.begin();
    auto it_root = root.begin();

    while (it_path != path.end() && it_root != root.end() && *it_path == *it_root) {
        ++it_path;
        ++it_root;
    }

    auto sub = bfs::path();
    while (it_path != path.end()) {
        sub /= *it_path;
        ++it_path;
    }

    return sub;
}

using seconds_t = std::chrono::seconds;
using sys_clock_t = std::chrono::system_clock;

std::int64_t to_unix(const fs_time_t &at) {
    auto sys_at = fs_time_t::clock::to_sys(at);
    return std::chrono::duration_cast<seconds_t>(sys_at.time_since_epoch()).count();
}

fs_time_t from_unix(std::int64_t at) {
    auto sys_time = sys_clock_t::from_time_t(at);
    return fs_time_t::clock::from_sys(sys_time);
}

} // namespace syncspirit::fs
