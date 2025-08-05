// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "model/diff/cluster_diff.h"
#include "model/folder_info.h"
#include "model/misc/orphaned_blocks.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API unshare_folder_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;

    unshare_folder_t(const model::cluster_t &cluster, model::folder_info_t &folder,
                     orphaned_blocks_t *orphaned_blocks = nullptr) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    utils::bytes_t peer_id;
};

} // namespace syncspirit::model::diff::modify
