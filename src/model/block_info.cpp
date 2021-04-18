#include "block_info.h"
#include <spdlog.h>

namespace syncspirit::model {

block_info_t::block_info_t(const db::BlockInfo &db_block, uint64_t db_key_) noexcept
    : hash{db_block.hash()}, weak_hash{db_block.weak_hash()}, size(db_block.size()), db_key{db_key_} {
    // sspdlog::trace("block {} is available", db_key);
}

block_info_t::block_info_t(const proto::BlockInfo &block) noexcept
    : hash{block.hash()}, weak_hash{block.weak_hash()}, size{block.size()} {
    mark_dirty();
}

db::BlockInfo block_info_t::serialize() noexcept {
    db::BlockInfo r;
    r.set_hash(hash);
    r.set_weak_hash(weak_hash);
    r.set_size(size);
    return r;
}

void block_info_t::link(file_info_t *file_info, size_t block_index) noexcept {
    file_blocks.emplace_back(file_info, block_index);
}

} // namespace syncspirit::model
