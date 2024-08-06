// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once
#include "../cluster_diff.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API load_cluster_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;
    using parent_t::parent_t;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
};

} // namespace syncspirit::model::diff::load
