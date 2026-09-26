// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "model/device_state.h"
#include "../cluster_diff.h"
#include "../cluster_visitor.h"

namespace syncspirit::model::diff::local {

struct SYNCSPIRIT_API local_state_update_t final : cluster_diff_t {

    local_state_update_t(const cluster_t &cluster, model::connection_state_t state) noexcept;

    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> apply_forward(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    model::connection_state_t state;
};

} // namespace syncspirit::model::diff::local
