#include "new_chunk_iterator.h"
#include "utils.h"
#include <algorithm>

using namespace syncspirit::fs;

new_chunk_iterator_t::new_chunk_iterator_t(scan_task_ptr_t task_, proto::FileInfo metadata_,
                                           file_ptr_t backend_) noexcept
    : task{std::move(task_)}, metadata{std::move(metadata_)}, backend{std::move(backend_)}, next_idx{0}, offset{0} {
    if (metadata.type() == proto::FileInfoType::FILE) {
        file_size = metadata.size();
        auto div = syncspirit::fs::get_block_size(file_size);
        unread_blocks = div.count;
        block_size = div.size;
        unread_bytes = file_size;
        hashes.resize(unread_blocks);
    } else {
        file_size = 0;
        unread_blocks = 0;
        block_size = 0;
        unread_bytes = 0;
    }
}

bool new_chunk_iterator_t::is_complete() const noexcept { return !unread_bytes && unfinished.empty(); }

auto new_chunk_iterator_t::read() noexcept -> outcome::result<details::chunk_t> {
    assert(unread_bytes);
    size_t next_sz = unread_bytes < block_size ? block_size : unread_bytes;
    auto r = backend->read(offset, next_sz);
    if (r) {
        offset += next_sz;
        auto idx = next_idx++;
        auto data = std::move(r.assume_value());
        unfinished.insert(idx);
        unread_bytes -= next_sz;
        return details::chunk_t{std::move(data), idx};
    }
    invalid = true;
    return r.assume_error();
}

bool new_chunk_iterator_t::has_more_chunks() const noexcept { return unread_bytes > 0; }

void new_chunk_iterator_t::ack(size_t block_index, uint32_t weak, std::string_view hash, int32_t block_size) noexcept {
    assert(block_index < hashes.size());
    assert(unfinished.count(block_index));
    hashes[block_index] = block_hash_t{std::string(hash), weak, block_size};
    unfinished.erase(block_index);
}
