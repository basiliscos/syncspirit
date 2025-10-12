// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "block_transaction.h"
#include "model/file_info.h"
#include "utils/bytes.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API append_block_t final : block_transaction_t {
    append_block_t(const file_info_t &file, const folder_info_t &fi, std::uint32_t block_index,
                   utils::bytes_t data) noexcept;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
    utils::bytes_t data;
};

} // namespace syncspirit::model::diff::modify
