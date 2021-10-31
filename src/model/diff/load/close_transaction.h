#pragma once

#include "../cluster_diff.h"
#include "../../../db/transaction.h"

namespace syncspirit::model::diff::load {

struct close_tranaction_t final : cluster_diff_t {
    close_tranaction_t(db::transaction_t txn) noexcept;
    ~close_tranaction_t() noexcept;

    outcome::result<void> apply(cluster_t&) const noexcept override;

    db::transaction_t txn;
};

} // namespace syncspirit::model::diff::cluster
