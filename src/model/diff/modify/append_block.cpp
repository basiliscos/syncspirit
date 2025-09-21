// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "append_block.h"
#include "utils/format.hpp"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

append_block_t::append_block_t(const file_info_t &file, const folder_info_t &fi, size_t block_index_, utils::bytes_t data_) noexcept
    : block_transaction_t{file, fi, block_index_}, data{std::move(data_)} {
    LOG_DEBUG(log, "append_block_t, file: '{}', block: {}", file, block_index);
}

auto append_block_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting append_block_t");
    return visitor(*this, custom);
}
