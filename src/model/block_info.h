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

struct SYNCSPIRIT_API block_info_t final : arc_base_t<block_info_t> {
    using removed_incides_t = std::vector<size_t>;
    using file_blocks_t = std::vector<file_block_t>;
    static const constexpr size_t digest_length = 32;
    static const constexpr size_t data_length = digest_length + 1;

    struct strict_hash_t {
        unsigned char data[data_length];
        utils::bytes_view_t get_hash() noexcept;
        utils::bytes_view_t get_key() noexcept;
    };

    static strict_hash_t make_strict_hash(utils::bytes_view_t hash) noexcept;

    static outcome::result<block_info_ptr_t> create(utils::bytes_view_t key, const db::BlockInfo &data) noexcept;
    static outcome::result<block_info_ptr_t> create(const proto::BlockInfo &block) noexcept;

    inline utils::bytes_view_t get_hash() const noexcept { return utils::bytes_view_t(hash + 1, digest_length); }
    inline utils::bytes_view_t get_key() const noexcept { return utils::bytes_view_t(hash); }
    inline std::uint32_t get_size() const noexcept { return size; }
    inline size_t usages() const noexcept { return file_blocks.size(); }
    inline file_blocks_t &get_file_blocks() { return file_blocks; }
    inline const file_blocks_t &get_file_blocks() const { return file_blocks; }

    proto::BlockInfo as_bep(size_t offset) const noexcept;
    utils::bytes_t serialize() const noexcept;

    void link(file_info_t *file_info, size_t block_index) noexcept;
    removed_incides_t unlink(file_info_t *file_info) noexcept;

    void mark_local_available(file_info_t *file_info) noexcept;
    file_block_t local_file() noexcept;

    bool is_locked() const noexcept;
    void lock() noexcept;
    void unlock() noexcept;

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

    unsigned char hash[data_length];
    file_blocks_t file_blocks;
    std::int32_t size = 0;
    std::uint32_t locked = 0;
};

struct SYNCSPIRIT_API block_infos_map_t : generic_map_t<block_info_ptr_t, 1> {
    block_info_ptr_t by_hash(utils::bytes_view_t view) const noexcept;
};

} // namespace syncspirit::model
