// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "block_info.h"
#include "file_info.h"
#include "proto/proto-helpers.h"
#include "db/prefix.h"
#include "misc/error_code.h"
#include <spdlog/spdlog.h>

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::block_info);

block_info_t::file_blocks_iterator_t::file_blocks_iterator_t(const block_info_t *block_info_,
                                                             std::uint32_t next_index_) noexcept
    : block_info{block_info_}, next_index{next_index_} {}

const file_block_t *block_info_t::file_blocks_iterator_t::next() noexcept {
    auto r = (const file_block_t *)(nullptr);
    if (block_info->counter & SINGLE_MASK) {
        if (next_index == 0) {
            ++next_index;
            r = &block_info->file_blocks_union.single;
        }
    } else {
        auto &fbs = block_info->file_blocks_union.multi;
        if (next_index < fbs.size()) {
            r = &fbs[next_index];
            ++next_index;
        }
    }
    return r;
}

std::uint32_t block_info_t::file_blocks_iterator_t::get_total() const noexcept {
    if (block_info->counter & SINGLE_MASK) {
        return 1;
    } else {
        auto &fbs = block_info->file_blocks_union.multi;
        return static_cast<std::uint32_t>(fbs.size());
    }
}

block_info_t::file_blocks_union_t::file_blocks_union_t() { new (&multi) file_blocks_t(); }

block_info_t::file_blocks_union_t::~file_blocks_union_t() {};

auto block_info_t::make_strict_hash(utils::bytes_view_t hash) noexcept -> strict_hash_t {
    assert(hash.size() <= digest_length);
    auto r = strict_hash_t{};
    memset(r.data, 0, digest_length);
    memcpy(r.data, hash.data(), hash.size());
    return r;
}

auto block_info_t::strict_hash_t::get_hash() noexcept -> utils::bytes_view_t {
    return utils::bytes_view_t(data, digest_length);
}

block_info_t::block_info_t(utils::bytes_view_t key) noexcept {
    auto h = key.subspan(1);
    std::copy(h.begin(), h.end(), hash);
}

block_info_t::block_info_t(const proto::BlockInfo &block) noexcept : size{proto::get_size(block)} {};

block_info_t::~block_info_t() {
    if (counter & SINGLE_MASK) {
        file_blocks_union.single.~file_block_t();
    } else {
        file_blocks_union.multi.~file_blocks_t();
    }
}

template <> void block_info_t::assign<db::BlockInfo>(const db::BlockInfo &block) noexcept {
    size = db::get_size(block);
}

outcome::result<block_info_ptr_t> block_info_t::create(utils::bytes_view_t key, const db::BlockInfo &data) noexcept {
    if (key.size() != digest_length + 1) {
        return make_error_code(error_code_t::invalid_block_key_length);
    }
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_block_prefix);
    }

    auto ptr = block_info_ptr_t(new block_info_t(key));
    ptr->assign(data);
    return outcome::success(ptr);
}

outcome::result<block_info_ptr_t> block_info_t::create(const proto::BlockInfo &block) noexcept {
    auto h = proto::get_hash(block);
    if (h.size() > digest_length) {
        return make_error_code(error_code_t::invalid_block_key_length);
    }

    auto ptr = block_info_ptr_t(new block_info_t(block));
    auto &h_ptr = ptr->hash;
    std::copy(h.begin(), h.end(), h_ptr);
    auto left = digest_length - h.size();
    if (left) {
        std::fill_n(h_ptr + h.size(), left, 0);
    }
    return outcome::success(ptr);
}

auto block_info_t::iterate_blocks(std::uint32_t start_index) const -> file_blocks_iterator_t {
    return file_blocks_iterator_t(this, start_index);
}

proto::BlockInfo block_info_t::as_bep(size_t offset) const noexcept {
    proto::BlockInfo r;
    proto::set_size(r, size);
    proto::set_hash(r, get_hash());
    proto::set_offset(r, offset);
    return r;
}

utils::bytes_t block_info_t::serialize() const noexcept { return db::encode(db::BlockInfo{size}); }

void block_info_t::link(file_info_t *file_info, size_t block_index) noexcept {
    bool use_multi = false;
    if (counter & SINGLE_MASK) {
        auto first = std::move(file_blocks_union.single);
        file_blocks_union.single.~file_block_t();
        new (&file_blocks_union.multi) file_blocks_t();
        counter = counter & ~SINGLE_MASK;
        file_blocks_union.multi.reserve(2);
        file_blocks_union.multi.emplace_back(std::move(first));
        use_multi = true;
    } else {
        if (file_blocks_union.multi.size()) {
            use_multi = true;
        } else {
            file_blocks_union.multi.~file_blocks_t();
            new (&file_blocks_union.single) file_block_t();
            file_blocks_union.single = {this, file_info, block_index};
            counter = counter | SINGLE_MASK;
        }
    }
    if (use_multi) {
        file_blocks_union.multi.emplace_back(this, file_info, block_index);
    }
}

auto block_info_t::unlink(file_info_t *file_info) noexcept -> removed_incides_t {
    removed_incides_t r;
    r.reserve(1);
    if (counter & SINGLE_MASK) {
        auto &fb = file_blocks_union.single;
        if (fb.matches(this, file_info)) {
            r.push_back(fb.block_index());
            fb.~file_block_t();
            new (&file_blocks_union.multi) file_blocks_t();
            counter = counter & ~SINGLE_MASK;
        }
    } else {
        auto &fbs = file_blocks_union.multi;
        for (auto it = fbs.begin(); it != fbs.end();) {
            auto &fb = *it;
            if (fb.matches(this, file_info)) {
                r.push_back(fb.block_index());
                it = fbs.erase(it);
            } else {
                ++it;
            }
        }
        if (fbs.size() == 1) {
            auto first = std::move(fbs.front());
            file_blocks_union.multi.~file_blocks_t();
            new (&file_blocks_union.single) file_block_t();
            counter = counter | SINGLE_MASK;
            file_blocks_union.single = std::move(first);
        }
    }
    assert(!r.empty() && "at least one block has been removed");
    return r;
}

void block_info_t::mark_local_available(file_info_t *file_info) noexcept {
    if (counter & SINGLE_MASK) {
        auto &fb = file_blocks_union.single;
        if (fb.matches(this, file_info)) {
            fb.mark_locally_available();
        }
    } else {
        for (auto &fb : file_blocks_union.multi) {
            if (fb.matches(this, file_info)) {
                fb.mark_locally_available();
                break;
            }
        }
    }
}

file_block_t block_info_t::local_file() const noexcept {
    auto it = iterate_blocks(0);
    while (auto fb = it.next()) {
        if (fb->is_locally_available()) {
            return *fb;
        }
    }
    return {};
}

bool block_info_t::is_locked() const noexcept { return counter & LOCK_MASK; }

void block_info_t::lock() noexcept {
    assert(!(counter & LOCK_MASK));
    counter = counter | LOCK_MASK;
}

void block_info_t::unlock() noexcept {
    assert((counter & LOCK_MASK));
    counter = counter & ~LOCK_MASK;
}

void block_info_t::refcouner_inc() const noexcept {
    auto save_bits = (LOCK_MASK | SINGLE_MASK) & counter;
    auto value = COUNTER_MASK & counter;
    ++value;
    counter = save_bits | value;
}

std::uint32_t block_info_t::refcouner_dec() const noexcept {
    auto save_bits = (LOCK_MASK | SINGLE_MASK) & counter;
    auto value = COUNTER_MASK & counter;
    --value;
    counter = save_bits | value;
    return value;
}

std::uint32_t block_info_t::use_count() const noexcept { return counter & COUNTER_MASK; }

block_info_ptr_t block_infos_map_t::by_hash(utils::bytes_view_t hash) const noexcept {
    auto &proj = get<0>();
    auto it = proj.find(hash);
    return it != proj.end() ? *it : block_info_ptr_t();
}

bool block_infos_map_t::put(const model::block_info_ptr_t &item, bool replace) noexcept {
    auto &proj = get<0>();
    return proj.emplace(item).second;
}

void block_infos_map_t::remove(const model::block_info_ptr_t &item) noexcept {
    parent_t::template get<0>().erase(item->get_hash());
}

} // namespace syncspirit::model
