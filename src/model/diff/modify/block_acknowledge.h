// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../block_diff.h"
#include "block_transaction.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API block_acknowledge_t final : block_diff_t {
    using parent_t = block_diff_t;

    block_acknowledge_t(const block_transaction_t &) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(block_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::modify
