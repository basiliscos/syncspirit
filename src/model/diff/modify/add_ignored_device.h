// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/ignored_device.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API add_ignored_device_t final : cluster_diff_t {
    add_ignored_device_t(const device_id_t &id, db::SomeDevice db_device) noexcept;
    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    device_id_t device_id;
    db::SomeDevice db_device;
};

} // namespace syncspirit::model::diff::modify
