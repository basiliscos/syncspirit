// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023-2024 Ivan Baidakou

#include "chunk_iterator.h"

using namespace syncspirit::fs;

chunk_iterator_t::chunk_iterator_t(scan_task_ptr_t task_, model::file_info_ptr_t file_, file_ptr_t backend_) noexcept
    : task{std::move(task_)}, peer_file{std::move(file_)}, backend{std::move(backend_)}, last_queued_block{0},
      abandoned{false}, valid_blocks_count{0} {
    unhashed_blocks = peer_file->get_blocks().size();
    valid_blocks_map.resize(unhashed_blocks);
}

bool chunk_iterator_t::has_more_chunks() const noexcept {
    return !abandoned && (last_queued_block < (int64_t)peer_file->get_blocks().size());
}

auto chunk_iterator_t::read() noexcept -> outcome::result<details::chunk_t> {
    assert(!abandoned);
    auto &i = last_queued_block;
    auto block_sz = peer_file->get_block_size();
    auto file_sz = peer_file->get_size();
    auto next_size = ((i + 1) * block_sz) > file_sz ? file_sz - (i * block_sz) : block_sz;
    auto block_opt = backend->read(i * block_sz, next_size);
    if (!block_opt) {
        auto ec = block_opt.assume_error();
        abandoned = true;

        return outcome::failure(ec);
    } else {
        auto &block = block_opt.value();
        auto idx = i++;
        return outcome::success(details::chunk_t{std::move(block), (size_t)idx});
    }
}

void chunk_iterator_t::ack_hashing() noexcept { --unhashed_blocks; }

void chunk_iterator_t::ack_block(std::string_view digest, size_t block_index) noexcept {
    auto &orig_block = peer_file->get_blocks().at(block_index);
    if (orig_block->get_hash() != digest) {
        return;
    }
    valid_blocks_map[block_index] = true;
    ++valid_blocks_count;
}
