// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "block_transaction.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API append_block_t final : block_transaction_t {

    append_block_t(const file_info_t &file, size_t block_index, std::string data) noexcept;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string data;
};

} // namespace syncspirit::model::diff::modify
