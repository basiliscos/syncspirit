// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <vector>
#include "cluster_diff.h"

namespace syncspirit::model::diff {

struct aggregate_t : cluster_diff_t {
    using diffs_t = std::vector<cluster_diff_ptr_t>;

    template <typename T> aggregate_t(T &&diffs_) noexcept : diffs(std::forward<T>(diffs_)) {}

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &) const noexcept override;

    diffs_t diffs;
};

} // namespace syncspirit::model::diff
