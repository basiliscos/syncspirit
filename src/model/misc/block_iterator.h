// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <vector>
#include "arc.hpp"
#include "../block_info.h"
#include "syncspirit-export.h"

namespace syncspirit::model {

struct folder_info_t;
struct file_info_t;

struct SYNCSPIRIT_API blocks_iterator_t : arc_base_t<blocks_iterator_t> {
    using blocks_t = std::vector<block_info_ptr_t>;

    blocks_iterator_t(file_info_t &source) noexcept;

    template <typename T> blocks_iterator_t &operator=(T &other) noexcept {
        source = other.source;
        i = other.i;
        return *this;
    }

    inline operator bool() noexcept { return source != nullptr; }

    file_block_t next(bool advance) noexcept;
    void reset() noexcept;

  private:
    void prepare() noexcept;
    void advance() noexcept;
    size_t i = 0;
    file_info_ptr_t source;
};

using block_iterator_ptr_t = intrusive_ptr_t<blocks_iterator_t>;

} // namespace syncspirit::model
