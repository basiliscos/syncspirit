// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/ignored_device.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API remove_ignored_device_t final : cluster_diff_t {
    remove_ignored_device_t(const ignored_device_t &device) noexcept;
    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    utils::bytes_view_t get_device_sha256() const noexcept;

    utils::bytes_t device_key;
};

} // namespace syncspirit::model::diff::modify
