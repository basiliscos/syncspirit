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

auto block_info_t::make_strict_hash(utils::bytes_view_t hash) noexcept -> strict_hash_t {
    assert(hash.size() <= digest_length);
    auto r = strict_hash_t{};
    r.data[0] = prefix;
    memset(r.data + 1, 0, digest_length);
    memcpy(r.data + 1, hash.data(), hash.size());
    return r;
}

auto block_info_t::strict_hash_t::get_hash() noexcept -> utils::bytes_view_t {
    return utils::bytes_view_t(data + 1, digest_length);
}

auto block_info_t::strict_hash_t::get_key() noexcept -> utils::bytes_view_t {
    return utils::bytes_view_t(data, data_length);
}

block_info_t::block_info_t(utils::bytes_view_t key) noexcept { std::copy(key.begin(), key.end(), hash); }

block_info_t::block_info_t(const proto::BlockInfo &block) noexcept : size{proto::get_size(block)} { hash[0] = prefix; }

template <> void block_info_t::assign<db::BlockInfo>(const db::BlockInfo &block) noexcept {
    size = db::get_size(block);
}

outcome::result<block_info_ptr_t> block_info_t::create(utils::bytes_view_t key, const db::BlockInfo &data) noexcept {
    if (key.size() != data_length) {
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
    std::copy(h.begin(), h.end(), h_ptr + 1);
    auto left = digest_length - h.size();
    if (left) {
        std::fill_n(h_ptr + 1 + h.size(), left, 0);
    }
    return outcome::success(ptr);
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
    file_blocks.emplace_back(this, file_info, block_index);
}

auto block_info_t::unlink(file_info_t *file_info) noexcept -> removed_incides_t {
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

bool block_info_t::is_locked() const noexcept { return locked != 0; }

void block_info_t::lock() noexcept {
    assert(!locked);
    ++locked;
}

void block_info_t::unlock() noexcept {
    assert(locked);
    --locked;
}

template <> SYNCSPIRIT_API utils::bytes_view_t get_index<0>(const block_info_ptr_t &item) noexcept {
    return item->get_hash();
}

block_info_ptr_t block_infos_map_t::by_hash(utils::bytes_view_t hash) const noexcept { return get(std::move(hash)); }

} // namespace syncspirit::model
