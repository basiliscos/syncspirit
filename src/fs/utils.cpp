// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "utils.h"
#include "utils/tls.h"
#include <zlib.h>

namespace syncspirit::fs {

static const std::string_view tmp_suffix = ".syncspirit-tmp";

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

relative_result_t relativize(const bfs::path &path, const boost::filesystem::path &root) noexcept {
    auto sub = bfs::relative(path, root);
    if (!is_temporal(path)) {
        return {sub, false};
    }
    auto str = sub.string();
    auto new_path = str.substr(0, str.size() - tmp_suffix.size());
    return {bfs::path(new_path), true};
}

} // namespace syncspirit::fs
