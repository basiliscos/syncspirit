// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "block_transaction.h"
#include "block_ack.h"
#include "block_rej.h"

using namespace syncspirit::model::diff::modify;

block_transaction_t::block_transaction_t(const file_info_t &file, const folder_info_t &fi, uint32_t block_index)
    : parent_t(file, fi, block_index) {}

auto block_transaction_t::ack() const -> cluster_diff_ptr_t { return new block_ack_t(*this); }

auto block_transaction_t::rej() const -> cluster_diff_ptr_t { return new block_rej_t(*this); }
