// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <optional>
#include "../cluster_diff.h"
#include "model/file_info.h"
#include "structs.pb.h"

namespace syncspirit::model::diff::modify {

struct local_update_t final : cluster_diff_t {
    using blocks_t = std::vector<proto::BlockInfo>;

    local_update_t(const file_info_t &file, db::FileInfo current, blocks_t current_blocks) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &) const noexcept override;

    std::string folder_id;
    std::string file_name;

    db::FileInfo prev;
    blocks_t prev_blocks;

    db::FileInfo current;
    blocks_t current_blocks;

    bool blocks_updated = false;
    std::unordered_set<std::string> removed_blocks;
};

} // namespace syncspirit::model::diff::modify
