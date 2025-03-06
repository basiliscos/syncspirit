// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/device.h"
#include "model/misc/uuid.h"
#include "proto/proto-helpers.h"
#include <vector>

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API add_pending_folders_t final : cluster_diff_t {
    struct item_t {
        db::PendingFolder db;
        utils::bytes_t peer_id;
        bu::uuid uuid;
    };
    using container_t = std::vector<item_t>;

    add_pending_folders_t(container_t items) noexcept;
    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    container_t container;
};

} // namespace syncspirit::model::diff::modify
