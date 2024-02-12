// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "append_block.h"
#include "../block_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model::diff::modify;

append_block_t::append_block_t(const file_info_t &file, size_t block_index_, std::string data_,
                               dispose_callback_t callback) noexcept
    : block_transaction_t{file, block_index_, std::move(callback)}, data{std::move(data_)} {}

auto append_block_t::visit(block_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting append_block_t");
    return visitor(*this, custom);
}
