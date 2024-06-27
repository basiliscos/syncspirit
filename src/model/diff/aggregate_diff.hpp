// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <vector>
#include "generic_diff.hpp"
#include "cluster_visitor.h"

namespace syncspirit::model::diff {

template <typename T> struct aggregate_diff_t : T {
    using ptr_t = diff_ptr_t<T>;
    using diffs_t = std::vector<ptr_t>;

    aggregate_diff_t() = default;

    template <typename U> aggregate_diff_t(U &&diffs_) noexcept : diffs(std::forward<U>(diffs_)) {}

    outcome::result<void> apply_impl(cluster_t &cluster) const noexcept override {
        for (auto &diff : diffs) {
            auto r = diff->apply(cluster);
            if (!r) {
                return r;
            }
        }
        return outcome::success();
    }

    outcome::result<void> visit(cluster_visitor_t &visitor, void *custom) const noexcept override {
        for (auto &diff : diffs) {
            auto r = diff->visit(visitor, custom);
            if (!r) {
                return r;
            }
        }
        return outcome::success();
    }

    diffs_t diffs;
};

} // namespace syncspirit::model::diff
