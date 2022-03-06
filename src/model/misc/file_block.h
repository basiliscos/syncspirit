// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "arc.hpp"

namespace syncspirit::model {

struct block_info_t;
struct file_info_t;
using block_info_ptr_t = intrusive_ptr_t<block_info_t>;

struct file_block_t {
    file_block_t() noexcept = default;
    file_block_t(file_block_t &&) noexcept = default;
    file_block_t(const file_block_t &) noexcept = default;
    file_block_t(block_info_t *block, file_info_t *file_info, std::size_t block_index) noexcept;

    file_block_t &operator=(const file_block_t &) noexcept;

    bool matches(const block_info_t *, const file_info_t *) noexcept;
    explicit operator bool() const noexcept;
    block_info_t *block() const noexcept { return block_info; }
    inline void mark_locally_available(bool value = true) noexcept { local = value; }
    inline bool is_locally_available() const noexcept { return local; }
    inline file_info_t *file() noexcept { return file_info; }
    inline const file_info_t *file() const noexcept { return file_info; }

    inline size_t block_index() const noexcept { return block_idx; }
    size_t get_offset() const noexcept;

  private:
    block_info_t *block_info;
    file_info_t *file_info;
    std::size_t block_idx;
    bool local = false;
};

} // namespace syncspirit::model
