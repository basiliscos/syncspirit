// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once
#include "../cluster_diff.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API load_cluster_t final : cluster_aggregate_diff_t {
    using parent_t = cluster_aggregate_diff_t;
    template <typename T> load_cluster_t(T &&diffs_) noexcept : parent_t(std::forward<T>(diffs_)) {}

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::load
