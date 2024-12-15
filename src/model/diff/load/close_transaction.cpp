// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "close_transaction.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::model::diff::load;

close_transaction_t::close_transaction_t(db::transaction_t txn_) noexcept : txn{std::move(txn_)} {
    LOG_DEBUG(log, "close_transaction_t::close_transaction_t()");
}

auto close_transaction_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    return applicator_t::apply_sibling(cluster);
}

close_transaction_t::~close_transaction_t() noexcept {
    LOG_DEBUG(log, "close_transaction_t::~close_transaction_t()");
    auto r = txn.commit();
    if (!r) {
        LOG_DEBUG(log, "close_transaction_t::~close_transaction_t, error: {}", r.error().message());
    }
}
