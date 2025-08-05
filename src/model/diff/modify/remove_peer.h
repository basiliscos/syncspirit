// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/device.h"
#include "model/misc/orphaned_blocks.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API remove_peer_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;

    remove_peer_t(const cluster_t &cluster, const device_t &peer) noexcept;
    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
    utils::bytes_view_t get_peer_sha256() const noexcept;

    utils::bytes_t peer_key;
};

} // namespace syncspirit::model::diff::modify
