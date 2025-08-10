// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "model/diff/cluster_diff.h"
#include "proto/proto-fwd.hpp"
#include <vector>

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API add_blocks_t : cluster_diff_t {
    using blocks_t = std::vector<proto::BlockInfo>;

    add_blocks_t(blocks_t blocks) noexcept;
    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    blocks_t blocks;
};

} // namespace syncspirit::model::diff::modify
