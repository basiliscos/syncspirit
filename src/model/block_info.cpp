#include "block_info.h"
#include "file_info.h"
#include <spdlog.h>

namespace syncspirit::model {

block_info_t::block_info_t(const db::BlockInfo &db_block, uint64_t db_key_) noexcept
    : hash{db_block.hash()}, weak_hash{db_block.weak_hash()}, size(db_block.size()), db_key{db_key_} {
    // sspdlog::trace("block {} is available", db_key);
}

block_info_t::block_info_t(const proto::BlockInfo &block) noexcept
    : hash{block.hash()}, weak_hash{block.weak_hash()}, size{block.size()}, db_key{0} {
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
    file_blocks.emplace_back(this, file_info, block_index);
}

auto block_info_t::unlink(file_info_t *file_info, bool deletion) noexcept -> removed_incides_t {
    removed_incides_t r;
    for (auto it = file_blocks.begin(); it != file_blocks.end();) {
        auto &fb = *it;
        if (fb.matches(this, file_info)) {
            r.push_back(fb.block_index());
            it = file_blocks.erase(it);
        } else {
            ++it;
        }
    }
    assert(!r.empty() && "at least one block has been removed");
    if (deletion && file_blocks.empty()) {
        mark_deleted();
    }
    return r;
}

void block_info_t::mark_local_available(file_info_t *file_info) noexcept {
    auto predicate = [&](file_block_t &block) { return block.matches(this, file_info); };
    auto it = std::find_if(file_blocks.begin(), file_blocks.end(), predicate);
    assert(it != file_blocks.end());
    it->mark_locally_available();
}

file_block_t block_info_t::local_file() noexcept {
    for (auto &b : file_blocks) {
        if (b.is_locally_available()) {
            return b;
        }
    }
    return {};
}

} // namespace syncspirit::model
