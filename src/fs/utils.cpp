// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "utils.h"
#include <cstdint>
#include "utils/path_view.hpp"

#if defined(__linux__)
#include <sys/vfs.h>
#include <linux/magic.h>
#include <cerrno>
#include <cstring>
#elif defined(__unix__)
#include <sys/param.h>
#include <sys/mount.h>
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <windows.h>
#endif

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

utils::metadata_comparator_t get_comparator(const utils::poly_path_view_t &path) noexcept {
    std::int32_t timestamp_precision_s = 0;
    bool permissions_supported = true;
    bool symlinks_supported = true;
#if defined(__linux__) || defined(__unix__)
    struct statfs data{};
    if (::statfs(path.get_full_name().data(), &data) == 0) {
#if defined(__linux__)
        if (data.f_type == MSDOS_SUPER_MAGIC) {
            timestamp_precision_s = 1;
            permissions_supported = false;
            symlinks_supported = false;
        }
#elif defined(__unix__)
        if (std::string_view("msdosfs") == sfs.f_fstypename) {
            timestamp_precision_s = 1;
            permissions_supported = false;
            symlinks_supported = false;
        }
#endif
    }
#endif
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    permissions_supported = false;
    symlinks_supported = false;
    auto wpath = path.get_full_wname(true);
    wchar_t root[MAX_PATH];
    wchar_t fs_name[MAX_PATH + 1];
    auto flags = DWORD{0};

    if (GetVolumePathNameW(wpath.data(), root, MAX_PATH)) {
        auto ok = GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, &flags, fs_name, MAX_PATH + 1);
        if (ok) {
            if (std::wstring_view(L"FAT32") == fs_name) {
                timestamp_precision_s = 1;
            }
        }
    }
#endif
    return {timestamp_precision_s, permissions_supported, symlinks_supported};
}

} // namespace syncspirit::fs
