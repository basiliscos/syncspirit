// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "utils.h"
#include "utils/tls.h"
#include <zlib.h>
#include <chrono>

namespace syncspirit::fs {

const std::string_view tmp_suffix = ".syncspirit-tmp";

static const std::size_t _block_sizes[] = {
    (1 << 7) * 1024,  (1 << 8) * 1024,  (1 << 9) * 1024,  (1 << 10) * 1024,
    (1 << 11) * 1024, (1 << 12) * 1024, (1 << 13) * 1024, (1 << 14) * 1024,
};

const std::size_t block_sizes_sz = 8;
const std::size_t *block_sizes = _block_sizes;

static const constexpr size_t max_blocks_count = 2000;

block_division_t get_block_size(size_t sz, int32_t prev_size) noexcept {
    size_t bs = 0;
    if (block_sizes[0] <= sz) {
        for (size_t i = 0; i < block_sizes_sz; ++i) {
            if (block_sizes[i] == (size_t)prev_size) {
                bs = prev_size;
                break;
            }
        }
    }

    if (!bs) {
        for (size_t i = 0; i < block_sizes_sz; ++i) {
            if (block_sizes[i] * max_blocks_count >= sz) {
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

    size_t count = 0;
    if (bs != 0) {
        count = sz / bs;
        if (count * bs < sz) {
            ++count;
        }
    }

    return {count, (int32_t)bs};
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

std::int64_t to_unix(const fs_time_t &at) { return sys_clock_t::to_time_t(fs_time_t::clock::to_sys(at)); }

fs_time_t from_unix(std::int64_t at) {
    auto sys_time = sys_clock_t::from_time_t(at);
    return fs_time_t::clock::from_sys(sys_time);
}

} // namespace syncspirit::fs
