// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "db/transaction.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API close_transaction_t final : cluster_diff_t {
    close_transaction_t(db::transaction_t txn) noexcept;
    ~close_transaction_t() noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;

    db::transaction_t txn;
};

} // namespace syncspirit::model::diff::load
