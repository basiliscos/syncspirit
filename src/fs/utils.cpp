// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "utils.h"
#include <cstdint>

namespace syncspirit::fs {

const std::string_view tmp_suffix = ".syncspirit-tmp";
const std::wstring_view tmp_wsuffix = L".syncspirit-tmp";

template <typename T> struct tmp_suffix_t;

template <> struct tmp_suffix_t<char> {
    inline static auto value = tmp_suffix;
};

template <> struct tmp_suffix_t<wchar_t> {
    inline static auto value = tmp_wsuffix;
};

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

bfs::path make_temporal(const bfs::path &path) noexcept {
    auto copy = path;
    copy += tmp_suffix.data();
    return copy;
}

template <typename T> static bool _is_temporal(const T *ptr, const T *end) {
    if ((end - ptr) >= tmp_suffix.size()) {
        auto ptr_1 = end - tmp_suffix.size();
        auto ptr_2 = tmp_suffix.data();
        for (size_t i = 0; i < tmp_suffix.size(); ++i, ++ptr_1, ++ptr_2) {
            if (*ptr_1 != *ptr_2) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool is_temporal(const bfs::path &path) noexcept {
    auto &str = path.native();
    return _is_temporal(str.data(), str.data() + str.size());
}

bool is_temporal(const std::string_view path) noexcept { return _is_temporal(path.data(), path.data() + path.size()); }

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
