// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "common.h"

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API unknown_devices_t final : cluster_diff_t {
    template <typename T> unknown_devices_t(T &&devices_) noexcept : devices{std::forward<T>(devices_)} {}

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    container_t devices;
};

} // namespace syncspirit::model::diff::load
