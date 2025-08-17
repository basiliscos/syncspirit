// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once
#include "model/diff/cluster_diff.h"
#include "model/diff/apply_controller.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API load_cluster_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;

    load_cluster_t(std::size_t blocks_count, std::size_t files_count) noexcept;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
    outcome::result<void> apply_forward(apply_controller_t &, void *) const noexcept override;

    std::size_t blocks_count;
    std::size_t files_count;
};

} // namespace syncspirit::model::diff::load
