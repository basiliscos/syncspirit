// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/diff/cluster_diff.h"
#include "model/folder_info.h"
#include "model/misc/orphaned_blocks.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API reset_folder_infos_t final : cluster_diff_t {
    reset_folder_infos_t(const folder_infos_map_t &map, orphaned_blocks_t *orphaned_blocks = nullptr);

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::modify
