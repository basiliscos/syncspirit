// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "common.h"
#include "structs.pb.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API file_infos_t final : cluster_diff_t {
    using item_t = std::pair<std::string_view, db::FileInfo>;
    using container_t = std::vector<item_t>;
    inline file_infos_t(container_t &&container_) noexcept : container{std::move(container_)} {}

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> apply_forward(cluster_t &, apply_controller_t &) const noexcept override;

    container_t container;
};

} // namespace syncspirit::model::diff::load
