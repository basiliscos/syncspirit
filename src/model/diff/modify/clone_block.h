// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "../block_diff.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::modify {

struct clone_block_t final : block_diff_t {
    clone_block_t(const file_info_t &target_file, block_info_t &block) noexcept;
    ~clone_block_t();

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(block_visitor_t &) const noexcept override;

    std::string source_device_id;
    std::string source_folder_id;
    std::string source_file_name;
    size_t source_block_index;

    // not for usage in apply
    block_info_t &orig_block;
};

} // namespace syncspirit::model::diff::modify
