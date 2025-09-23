// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "misc/arc.hpp"
#include "misc/map.hpp"
#include "misc/file_block.h"
#include "proto/proto-fwd.hpp"
#include "utils/bytes.h"
#include "syncspirit-export.h"
#include <boost/outcome.hpp>
#include <cstdint>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct file_info_t;
using file_info_ptr_t = intrusive_ptr_t<file_info_t>;
struct block_info_t;
using block_info_ptr_t = intrusive_ptr_t<block_info_t>;

struct SYNCSPIRIT_API block_info_t {
    using removed_incides_t = std::vector<size_t>;
    using file_blocks_t = std::vector<file_block_t>;
    static const constexpr size_t digest_length = 32;
    static constexpr std::uint32_t LOCK_MASK = 1 << 31;
    static constexpr std::uint32_t SINGLE_MASK = 1 << 30;
    static constexpr std::uint32_t COUNTER_MASK = ~(LOCK_MASK | SINGLE_MASK);

    struct strict_hash_t {
        unsigned char data[digest_length];
        utils::bytes_view_t get_hash() noexcept;
    };

    static strict_hash_t make_strict_hash(utils::bytes_view_t hash) noexcept;

    static outcome::result<block_info_ptr_t> create(utils::bytes_view_t key, const db::BlockInfo &data) noexcept;
    static outcome::result<block_info_ptr_t> create(const proto::BlockInfo &block) noexcept;

    ~block_info_t();

    struct file_blocks_iterator_t {

        file_blocks_iterator_t(block_info_t *block_info, std::uint32_t next) noexcept;

        file_blocks_iterator_t(file_blocks_iterator_t &&) = default;
        file_blocks_iterator_t(const file_blocks_iterator_t &) = delete;

        file_blocks_iterator_t &operator=(file_blocks_iterator_t &&);

        const file_block_t *next() noexcept;
        std::uint32_t get_total() const noexcept;

      private:
        const block_info_t *block_info;
        std::uint32_t next_index;
    };

    inline utils::bytes_view_t get_hash() const noexcept { return utils::bytes_view_t(hash, digest_length); }
    inline std::uint32_t get_size() const noexcept { return size; }
    std::uint32_t usages() const noexcept;

    file_blocks_iterator_t iterate_blocks(std::uint32_t start_index = 0);

    proto::BlockInfo as_bep(size_t offset) const noexcept;
    utils::bytes_t serialize() const noexcept;

    void link(file_info_t *file_info, size_t block_index) noexcept;
    removed_incides_t unlink(file_info_t *file_info) noexcept;

    void mark_local_available(file_info_t *file_info) noexcept;
    file_block_t local_file() noexcept;

    bool is_locked() const noexcept;
    void lock() noexcept;
    void unlock() noexcept;

    void refcouner_inc() const noexcept;
    std::uint32_t refcouner_dec() const noexcept;
    std::uint32_t use_count() const noexcept;

    inline bool operator==(const block_info_t &right) const noexcept {
        auto lh = get_hash();
        auto rh = right.get_hash();
        return std::equal(lh.begin(), lh.end(), rh.begin(), rh.end());
    }
    inline bool operator!=(const block_info_t &right) const noexcept { return !(*this == right); }

  private:
    template <typename T> void assign(const T &item) noexcept;
    block_info_t(utils::bytes_view_t key) noexcept;
    block_info_t(const proto::BlockInfo &block) noexcept;

    union file_blocks_union_t {
        file_blocks_union_t();
        ~file_blocks_union_t();
        file_block_t single;
        file_blocks_t multi;
    };

    unsigned char hash[digest_length];
    file_blocks_union_t file_blocks_union;
    std::int32_t size = 0;
    mutable std::uint32_t counter = 0;
};

inline void intrusive_ptr_add_ref(const block_info_t *ptr) noexcept { ptr->refcouner_inc(); }

inline void intrusive_ptr_release(const block_info_t *ptr) noexcept {
    if (ptr->refcouner_dec() == 0) {
        delete ptr;
    }
}

// clang-format off
namespace block_details {

namespace mi = boost::multi_index;

inline utils::bytes_view_t get_hash(const model::block_info_ptr_t &block) noexcept {
    return block->get_hash();
}

// clang-format off
using block_map_base_t = mi::multi_index_container<
    model::block_info_ptr_t,
    mi::indexed_by<
        mi::hashed_unique<mi::global_fun<const model::block_info_ptr_t &, utils::bytes_view_t, &get_hash>>
    >
>;
}
// clang-format on

struct SYNCSPIRIT_API block_infos_map_t : private block_details::block_map_base_t {
    using parent_t = block_details::block_map_base_t;
    block_infos_map_t() = default;
    block_infos_map_t(block_infos_map_t &) = delete;

    using parent_t::begin;
    using parent_t::clear;
    using parent_t::end;
    using parent_t::size;

    block_info_ptr_t by_hash(utils::bytes_view_t view) const noexcept;
    bool put(const model::block_info_ptr_t &item, bool replace = false) noexcept;
    void remove(const model::block_info_ptr_t &item) noexcept;
};

} // namespace syncspirit::model
