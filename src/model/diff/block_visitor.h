// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "syncspirit-export.h"

namespace syncspirit::model::diff {

struct block_diff_t;

namespace modify {
struct append_block_t;
struct block_ack_t;
struct block_rej_t;
struct block_transaction_t;
struct clone_block_t;
} // namespace modify

namespace local {
struct blocks_availability_t;
}

template <> struct SYNCSPIRIT_API generic_visitor_t<tag::block, block_diff_t> {
    virtual outcome::result<void> operator()(const modify::append_block_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::block_ack_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::block_rej_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::blocks_availability_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::clone_block_t &, void *custom) noexcept;
};

using block_visitor_t = generic_visitor_t<tag::block, block_diff_t>;

} // namespace syncspirit::model::diff
