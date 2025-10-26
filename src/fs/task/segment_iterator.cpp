// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "segment_iterator.h"
#include "hasher/messages.h"

using namespace syncspirit::fs;
using namespace syncspirit::fs::task;

segment_iterator_t::segment_iterator_t(const r::address_ptr_t &hasher_addr_, const r::address_ptr_t &back_addr_,
                                       hasher::payload::extendended_context_prt_t context_, bfs::path path_,
                                       std::int64_t offset_, std::int32_t block_index_, std::int32_t block_count_,
                                       std::int32_t block_size_, std::int32_t last_block_size_) noexcept
    : hasher_addr{hasher_addr_}, back_addr{back_addr_}, context{std::move(context_)}, path{std::move(path_)},
      offset{offset_}, block_index{block_index_}, block_count{block_count_}, block_size{block_size_},
      last_block_size{last_block_size_} {}

void segment_iterator_t::process(fs_slave_t &fs_slave, r::actor_base_t &host) noexcept {
    assert(!ec);
    if (!file.has_backend()) {
        auto opt = file_t::open_read(path);
        if (!opt.has_value()) {
            ec = opt.assume_error();
            return;
        }
        file = std::move(opt.assume_value());
    }

    for (std::int32_t i = block_index; i < block_count; ++i) {
        auto bs = (i + 1 == block_count) ? last_block_size : block_size;
        auto block_opt = file.read(offset, bs);
        if (!block_opt) {
            std::abort();
        }
        auto bytes = std::move(block_opt).value();
        host.route<hasher::payload::digest_t>(hasher_addr, back_addr, std::move(bytes), i, context);
        ++current_block;
    }
}
