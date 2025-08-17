// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/device_id.h"
#include "proto/proto-helpers.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API add_ignored_device_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;

    add_ignored_device_t(const cluster_t &cluster, const device_id_t &id, db::SomeDevice db_device) noexcept;
    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> apply_forward(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    device_id_t device_id;
    db::SomeDevice db_device;
};

} // namespace syncspirit::model::diff::modify
