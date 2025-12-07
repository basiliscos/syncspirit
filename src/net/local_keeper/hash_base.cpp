// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "hash_base.h"
#include "fs/utils.h"

using namespace syncspirit::net::local_keeper;

hash_base_t::hash_base_t(child_info_t &&info_, std::int32_t block_size_) : child_info_t{std::move(info_)} {
    if (block_size_ == 0) {
        auto div = fs::get_block_size(size, 0);
        block_size = div.size;
        unprocessed_blocks = unhashed_blocks = total_blocks = div.count;
    } else {
        block_size = block_size_;
        auto count = size / static_cast<decltype(size)>(block_size_);
        if (size % count) {
            ++count;
        }
        unprocessed_blocks = unhashed_blocks = total_blocks = count;
    }
    blocks.resize(total_blocks);
}

bool hash_base_t::commit_error(sys::error_code ec_, std::int32_t delta) {
    ec = ec_;
    errored_blocks += delta;
    return errored_blocks + unprocessed_blocks == unhashed_blocks;
}

bool hash_base_t::commit_hash() const { return errored_blocks + unprocessed_blocks + unhashed_blocks == total_blocks; }
