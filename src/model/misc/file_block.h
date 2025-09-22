// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "arc.hpp"
#include "syncspirit-export.h"
#include <cstdint>

namespace syncspirit::model {

struct block_info_t;
struct file_info_t;
using block_info_ptr_t = intrusive_ptr_t<block_info_t>;

struct SYNCSPIRIT_API file_block_t {
    file_block_t() noexcept = default;
    file_block_t(file_block_t &&) noexcept = default;
    file_block_t(const file_block_t &) noexcept = default;
    file_block_t(block_info_t *block, file_info_t *file_info, std::size_t block_index) noexcept;

    file_block_t &operator=(const file_block_t &) noexcept;

    bool matches(const block_info_t *, const file_info_t *) noexcept;
    explicit operator bool() const noexcept;
    block_info_t *block() const noexcept { return block_info; }

    inline file_info_t *file() noexcept { return file_info; }
    inline const file_info_t *file() const noexcept { return file_info; }

    void mark_locally_available(bool value = true) noexcept;
    bool is_locally_available() const noexcept;
    std::uint32_t block_index() const noexcept;

    size_t get_offset() const noexcept;

  private:
    static constexpr std::uint32_t LOCAL_MASK = 1 << 31;
    static constexpr std::uint32_t INDEX_MASK = ~LOCAL_MASK;

    block_info_t *block_info;
    file_info_t *file_info;
    std::uint32_t block_idx;
};

} // namespace syncspirit::model
