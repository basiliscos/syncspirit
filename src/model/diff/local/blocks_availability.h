// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "../block_diff.h"
#include "model/file_info.h"
#include <vector>

namespace syncspirit::model::diff::local {

struct SYNCSPIRIT_API blocks_availability_t final : block_diff_t {
    using valid_blocks_map_t = std::vector<bool>;

    blocks_availability_t(const file_info_t &file, valid_blocks_map_t valid_blocks_map) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    valid_blocks_map_t valid_blocks_map;
};

} // namespace syncspirit::model::diff::local
