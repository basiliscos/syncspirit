// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "block_transaction.h"

using namespace syncspirit::model::diff::modify;

block_transaction_t::block_transaction_t(const file_info_t &file, size_t block_index, dispose_callback_t callback_)
    : parent_t(file, block_index), callback(std::move(callback_)) {}

block_transaction_t::~block_transaction_t() { callback(*this); }

auto block_transaction_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    return next ? next->apply(cluster) : outcome::success();
}
