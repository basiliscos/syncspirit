// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../aggregate.h"
#include "model/device.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API remove_peer_t final : aggregate_t {

    remove_peer_t(const cluster_t &cluster, const device_t &peer) noexcept;
    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string peer_id;
};

} // namespace syncspirit::model::diff::modify
