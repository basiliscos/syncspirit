// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "syncspirit-export.h"

namespace syncspirit::model {

struct file_info_t;

namespace diff {

struct block_diff_t;
using block_diff_ptr_t = boost::intrusive_ptr<block_diff_t>;

struct SYNCSPIRIT_API block_diff_t : generic_diff_t<tag::block, block_diff_t> {
    block_diff_t(const block_diff_t &) noexcept;
    block_diff_t(const file_info_t &file, size_t block_index = 0) noexcept;

#if 0
    virtual outcome::result<void> visit(visitor_t &, void *custom) const noexcept override;
    block_diff_t *assign_sibling(block_diff_t *sibling) noexcept;
    void assign_child(block_diff_ptr_t child) noexcept;
#endif

    std::string file_name;
    std::string folder_id;
    std::string device_id;
    size_t block_index;
};

using block_visitor_t = block_diff_t::visitor_t;

} // namespace diff
} // namespace syncspirit::model
