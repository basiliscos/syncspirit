// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../block_diff.h"
#include "block_transaction.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API block_rej_t final : block_diff_t {
    using parent_t = block_diff_t;

    block_rej_t(const block_transaction_t &) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::modify
