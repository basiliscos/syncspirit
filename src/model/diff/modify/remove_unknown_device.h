// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/unknown_device.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API remove_unknown_device_t final : cluster_diff_t {
    remove_unknown_device_t(const unknown_device_t &device) noexcept;
    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string_view get_device_sha256() const noexcept;

    std::string device_key;
};

} // namespace syncspirit::model::diff::modify
