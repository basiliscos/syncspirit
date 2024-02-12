// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023 Ivan Baidakou

#include "chunk_iterator.h"

using namespace syncspirit::fs;

static auto non_zero = [](char it) { return it != 0; };

chunk_iterator_t::chunk_iterator_t(scan_task_ptr_t task_, model::file_info_ptr_t file_,
                                   model::file_info_ptr_t source_file_, file_ptr_t backend_) noexcept
    : task{std::move(task_)}, file{std::move(file_)}, source_file{std::move(source_file_)},
      backend{std::move(backend_)}, last_queued_block{0}, valid_blocks{-1}, queue_size{0}, out_of_order{},
      abandoned{false}, invalid{false} {
    unhashed_blocks = source_file->get_blocks().size();
}

bool chunk_iterator_t::has_more_chunks() const noexcept {
    return !abandoned && !invalid && (last_queued_block < (int64_t)source_file->get_blocks().size());
}

auto chunk_iterator_t::read() noexcept -> outcome::result<details::chunk_t> {
    assert(!abandoned);
    auto &i = last_queued_block;
    auto block_sz = source_file->get_block_size();
    auto file_sz = source_file->get_size();
    auto next_size = ((i + 1) * block_sz) > file_sz ? file_sz - (i * block_sz) : block_sz;
    auto block_opt = backend->read(i * block_sz, next_size);
    if (!block_opt) {
        auto ec = block_opt.assume_error();
        abandoned = true;

        return outcome::failure(ec);
    } else {
        auto &block = block_opt.value();
        auto it = std::find_if(block.begin(), block.end(), non_zero);
        if (it == block.end()) { // we have only zeroes
            abandoned = true;
            unhashed_blocks -= (source_file->get_blocks().size() - i);
            return outcome::success(details::chunk_t{{}, 0});
        } else {
            ++queue_size;
            auto idx = i++;
            return outcome::success(details::chunk_t{std::move(block), (size_t)idx});
        }
    }
}

void chunk_iterator_t::ack_hashing() noexcept {
    --queue_size;
    --unhashed_blocks;
}

auto chunk_iterator_t::ack_block(std::string_view digest, size_t block_index) noexcept -> bool {
    auto &orig_block = source_file->get_blocks().at(block_index);
    if (orig_block->get_hash() != digest) {
        invalid = true;
        return false;
    }

    auto &ooo = out_of_order;
    if ((int64_t)block_index == valid_blocks + 1) {
        ++valid_blocks;
    } else {
        ooo.insert((int64_t)block_index);
    }

    auto it = ooo.begin();
    while (*it == valid_blocks + 1) {
        ++valid_blocks;
        it = ooo.erase(it);
    }

    return true;
}
