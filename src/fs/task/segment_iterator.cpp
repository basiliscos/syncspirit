// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "segment_iterator.h"
#include "hasher/messages.h"
#include "hasher/hasher_plugin.h"
#include "fs/utils.h"
#include "utils/path_view.hpp"
#include "utils/path_utils.h"
#include <boost/system/errc.hpp>

using namespace syncspirit::fs;
using namespace syncspirit::fs::task;

segment_iterator_t::segment_iterator_t(const r::address_ptr_t &back_addr_,
                                       hasher::payload::extendended_context_prt_t context_, utils::path_t path_,
                                       std::int64_t offset_, std::int32_t block_index_, std::int32_t block_count_,
                                       std::int32_t block_size_, std::int32_t last_block_size_,
                                       std::int64_t last_write_time_) noexcept
    : back_addr{back_addr_}, context{std::move(context_)}, path{std::move(path_)}, offset{offset_},
      block_index{block_index_}, block_count{block_count_}, block_size{block_size_}, last_block_size{last_block_size_},
      last_write_time{last_write_time_} {
    assert(block_count > 0);
}

bool segment_iterator_t::process(fs_slave_t &fs_slave, execution_context_t &exec_ctx) noexcept {
    using byte_chunks_t = std::pmr::vector<utils::bytes_t>;

    assert(!ec);
    auto path_view = path.get_view(exec_ctx.allocator);
    if (!file.has_backend()) {
        auto opt = file_t::open_read(path_view);
        if (!opt.has_value()) {
            ec = opt.assume_error();
            return false;
        }
        file = std::move(opt.assume_value());
    }

    auto modified = utils::last_write_time(path_view, ec);
    if (ec) {
        return false;
    }

    if (modified != last_write_time) {
        ec = utils::make_error_code(utils::error_code_t::concurrent_file_modification);
        return false;
    }

    auto byte_chunks = byte_chunks_t(exec_ctx.allocator);

    for (std::int32_t j = 0; j < block_count && !ec; ++j) {
        auto bs = (j + 1 == block_count) ? last_block_size : block_size;
        auto off = offset + std::int64_t{block_size} * j;
        auto block_opt = file.read(off, bs);
        ++current_block;
        if (!block_opt) {
            if (errno) {
                ec = std::error_code{errno, std::system_category()};
            } else {
                ec = std::make_error_code(std::errc::io_error);
            }
            return false;
        } else {
            auto bytes = std::move(block_opt).value();
            byte_chunks.emplace_back(std::move(bytes));
        }
    }

    for (std::int32_t i = block_index, j = 0; j < block_count && !ec; ++i, ++j) {
        auto &bytes = byte_chunks[j];
        exec_ctx.plugin->calc_digest(std::move(bytes), i, back_addr, context);
    }

    return false;
}
