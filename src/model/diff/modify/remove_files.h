// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "model/file_info.h"
#include "model/misc/orphaned_blocks.h"
#include "generic_remove.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API remove_files_t final : generic_remove_t {
    using generic_remove_t::generic_remove_t;

    remove_files_t(const folder_info_t& fi, const file_infos_map_t &files,
                   orphaned_blocks_t *orphaned_blocks = nullptr) noexcept;

    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    utils::bytes_t device_id;
    std::string folder_id;
};

} // namespace syncspirit::model::diff::modify
