// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/device_id.h"
#include "proto/proto-fwd.hpp"

namespace syncspirit::model::diff::contact {

struct SYNCSPIRIT_API ignored_connected_t final : cluster_diff_t {
    ignored_connected_t(cluster_t &, const model::device_id_t &device_id, db::SomeDevice db_device) noexcept;

    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    model::device_id_t device_id;
    db::SomeDevice db_device;
};

} // namespace syncspirit::model::diff::contact
