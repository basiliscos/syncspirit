// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../contact_diff.h"
#include "model/ignored_device.h"

namespace syncspirit::model::diff::contact {

struct SYNCSPIRIT_API ignored_connected_t final : contact_diff_t {
    ignored_connected_t(cluster_t &cluster, const model::device_id_t &device_id, db::SomeDevice db_device) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(contact_visitor_t &, void *) const noexcept override;

    model::device_id_t device_id;
    db::SomeDevice db_device;
};

} // namespace syncspirit::model::diff::contact
