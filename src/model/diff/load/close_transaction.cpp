// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "close_transaction.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::model::diff::load;

close_transaction_t::close_transaction_t(db::transaction_t txn_) noexcept : txn{std::move(txn_)} {}

auto close_transaction_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    return outcome::success();
}

close_transaction_t::~close_transaction_t() noexcept {
    auto r = txn.commit();
    if (!r) {
        spdlog::critical("error closing tranaction:: {}", r.error().message());
    }
}
