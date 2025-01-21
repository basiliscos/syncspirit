// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <vector>
#include "arc.hpp"
#include "model/block_info.h"
#include "syncspirit-export.h"

namespace syncspirit::model {

struct folder_info_t;
struct file_info_t;

struct SYNCSPIRIT_API blocks_iterator_t : arc_base_t<blocks_iterator_t> {
    using blocks_t = std::vector<block_info_ptr_t>;

    blocks_iterator_t(file_info_t &source) noexcept;
    operator bool() const noexcept;

    file_block_t next() noexcept;
    file_info_t *get_source() noexcept;

  private:
    void prepare() noexcept;
    void advance() noexcept;
    size_t i = 0;
    std::int64_t sequence;
    file_info_ptr_t source;
};

using block_iterator_ptr_t = intrusive_ptr_t<blocks_iterator_t>;

} // namespace syncspirit::model
