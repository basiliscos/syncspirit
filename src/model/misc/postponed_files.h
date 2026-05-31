// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

#include "model/block_info.h"
#include "model/file_info.h"

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/identity.hpp>

namespace syncspirit::model {

namespace mi = boost::multi_index;

struct SYNCSPIRIT_API postponed_files_t {
    using files_t = std::unordered_set<model::file_info_ptr_t>;

    postponed_files_t() = default;
    postponed_files_t(const postponed_files_t &) = delete;
    postponed_files_t(postponed_files_t &&) = delete;

    void postpone(model::block_info_ptr_t block, model::file_info_ptr_t file) noexcept;
    void advance(model::block_info_ptr_t &block) noexcept;
    model::file_info_ptr_t get_ready() noexcept;
    void forget(model::file_info_t *) noexcept;
    void clear() noexcept;

  private:
    struct block_2_file_t {
        model::block_info_ptr_t block;
        model::file_info_ptr_t file;
    };
    struct block_2_file_ptr_t {
        model::block_info_t *block;
        model::file_info_t *file;
    };
    struct hasher_t {
        std::size_t operator()(const block_2_file_t &) const noexcept;
        std::size_t operator()(const block_2_file_ptr_t &) const noexcept;
    };
    struct eq_t {
        bool operator()(const block_2_file_t &a, const block_2_file_t &b) const noexcept;
        bool operator()(const block_2_file_ptr_t &a, const block_2_file_t &b) const noexcept;
        bool operator()(const block_2_file_t &a, const block_2_file_ptr_t &b) const noexcept;
        bool operator()(const block_2_file_ptr_t &a, const block_2_file_ptr_t &b) const noexcept;
    };

    using block_2_files_t = mi::multi_index_container<
        block_2_file_t,
        mi::indexed_by<
            mi::hashed_unique<mi::identity<block_2_file_t>, hasher_t, eq_t>,
            mi::ordered_non_unique<mi::member<block_2_file_t, model::block_info_ptr_t, &block_2_file_t::block>>,
            mi::ordered_non_unique<mi::member<block_2_file_t, model::file_info_ptr_t, &block_2_file_t::file>>>>;

    block_2_files_t block_2_files;
    files_t ready;
};

} // namespace syncspirit::model
