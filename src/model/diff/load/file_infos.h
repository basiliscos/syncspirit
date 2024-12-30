// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "common.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API file_infos_t final : cluster_diff_t {
    template <typename T> file_infos_t(T &&container_) noexcept : container{std::forward<T>(container_)} {}

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> apply_forward(cluster_t &, apply_controller_t &) const noexcept override;

    container_t container;
};

} // namespace syncspirit::model::diff::load
