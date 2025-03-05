// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "new_chunk_iterator.h"
#include "utils.h"
#include <algorithm>

using namespace syncspirit::fs;

new_chunk_iterator_t::new_chunk_iterator_t(scan_task_ptr_t task_, proto::FileInfo metadata_,
                                           file_ptr_t backend_) noexcept
    : task{std::move(task_)}, metadata{std::move(metadata_)}, backend{std::move(backend_)}, next_idx{0}, offset{0} {
    using namespace pp;
    if (proto::get_type(metadata) == proto::FileInfoType::FILE) {
        file_size = proto::get_size(metadata);
        auto bs = proto::get_block_size(metadata);
        auto div = syncspirit::fs::get_block_size(file_size, bs);
        unread_blocks = div.count;
        block_size = div.size;
        unread_bytes = file_size;
        hashes.resize(unread_blocks);
        assert(!(unread_bytes && !block_size));
    } else {
        file_size = 0;
        unread_blocks = 0;
        block_size = 0;
        unread_bytes = 0;
    }
}

bool new_chunk_iterator_t::is_complete() const noexcept {
    return !unread_blocks && !unread_bytes && unfinished.empty();
}

auto new_chunk_iterator_t::read() noexcept -> outcome::result<details::chunk_t> {
    assert(unread_bytes);
    size_t next_sz = std::min(block_size, unread_bytes);
    assert(next_sz);
    auto r = backend->read(offset, next_sz);
    if (r) {
        offset += next_sz;
        auto idx = next_idx++;
        auto data = std::move(r.assume_value());
        unfinished.insert(idx);
        if (next_sz) {
            unread_bytes -= next_sz;
            assert(unread_blocks);
            --unread_blocks;
        }
        return details::chunk_t{std::move(data), idx};
    }
    invalid = true;
    return r.assume_error();
}

bool new_chunk_iterator_t::has_more_chunks() const noexcept { return unread_bytes > 0; }

void new_chunk_iterator_t::ack(size_t block_index, uint32_t weak, utils::bytes_t hash, int32_t block_size) noexcept {
    assert(block_index < hashes.size());
    assert(unfinished.count(block_index));
    assert(!hash.empty());
    hashes[block_index] = block_hash_t{hash, weak, block_size};
    unfinished.erase(block_index);
}
