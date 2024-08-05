// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "cluster_diff.h"
#include "../cluster.h"
#include <cassert>

using namespace syncspirit::model::diff;

cluster_diff_t::cluster_diff_t(cluster_diff_ptr_t next_) noexcept : next{std::move(next_)} {}

cluster_diff_t *cluster_diff_t::assign(cluster_diff_t *next_) noexcept {
    assert(!next);
    next = next_;

    auto n = next_;
    while (n->next) {
        n = n->next.get();
    }
    return n;
}

auto cluster_diff_t::visit(cluster_visitor_t &, void *) const noexcept -> outcome::result<void> {
    return outcome::success();
}
