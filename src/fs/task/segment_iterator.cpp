// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "segment_iterator.h"
#include "hasher/messages.h"
#include "hasher/hasher_plugin.h"

using namespace syncspirit::fs;
using namespace syncspirit::fs::task;

segment_iterator_t::segment_iterator_t(const r::address_ptr_t &back_addr_,
                                       hasher::payload::extendended_context_prt_t context_, bfs::path path_,
                                       std::int64_t offset_, std::int32_t block_index_, std::int32_t block_count_,
                                       std::int32_t block_size_, std::int32_t last_block_size_) noexcept
    : back_addr{back_addr_}, context{std::move(context_)}, path{std::move(path_)}, offset{offset_},
      block_index{block_index_}, block_count{block_count_}, block_size{block_size_}, last_block_size{last_block_size_} {
}

void segment_iterator_t::process(fs_slave_t &fs_slave, hasher::hasher_plugin_t *hasher) noexcept {
    assert(!ec);
    if (!file.has_backend()) {
        auto opt = file_t::open_read(path);
        if (!opt.has_value()) {
            ec = opt.assume_error();
            return;
        }
        file = std::move(opt.assume_value());
    }

    for (std::int32_t i = block_index, j = 0; j < block_count; ++i, ++j) {
        auto bs = (j + 1 == block_count) ? last_block_size : block_size;
        auto off = offset + std::int64_t{block_size} * j;
        auto block_opt = file.read(off, bs);
        if (!block_opt) {
            std::abort();
        }
        auto bytes = std::move(block_opt).value();
        hasher->calc_digest(std::move(bytes), i, back_addr, context);
        ++current_block;
    }
}
