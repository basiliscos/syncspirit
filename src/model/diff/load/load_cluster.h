// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once
#include "model/diff/cluster_diff.h"
#include "model/diff/apply_controller.h"
#include "db/transaction.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API load_cluster_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;

    load_cluster_t(db::transaction_t txn, std::size_t blocks_count, std::size_t files_count) noexcept;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> apply_forward(cluster_t &, apply_controller_t &) const noexcept override;

    db::transaction_t txn;
    std::size_t blocks_count;
    std::size_t files_count;
};

} // namespace syncspirit::model::diff::load
