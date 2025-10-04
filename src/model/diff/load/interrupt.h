// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once
#include "model/diff/cluster_diff.h"
#include "model/diff/apply_controller.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API interrupt_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;

    using parent_t::parent_t;

    outcome::result<void> apply_forward(apply_controller_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::load
