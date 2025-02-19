// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "common.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API blocks_t final : cluster_diff_t {
    template <typename T> blocks_t(T &&blocks_) noexcept : blocks{std::forward<T>(blocks_)} {}

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> apply_forward(cluster_t &, apply_controller_t &) const noexcept override;

    container_t blocks;
};

} // namespace syncspirit::model::diff::load
