// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "generic_remove.h"
#include "model/folder_info.h"
#include "model/misc/orphaned_blocks.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API remove_folder_infos_t final : generic_remove_t {
    remove_folder_infos_t(const folder_infos_map_t &map, orphaned_blocks_t *orphaned_blocks = nullptr);

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;
};

} // namespace syncspirit::model::diff::modify
