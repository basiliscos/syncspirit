// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "aggregate.h"

using namespace syncspirit::model::diff;

auto aggregate_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    for (auto &diff : diffs) {
        auto r = diff->apply(cluster);
        if (!r) {
            return r;
        }
    }
    return outcome::success();
}

auto aggregate_t::visit(cluster_visitor_t &vistor, void *custom) const noexcept -> outcome::result<void> {
    for (auto &diff : diffs) {
        auto r = diff->visit(vistor, custom);
        if (!r) {
            return r;
        }
    }
    return outcome::success();
}
