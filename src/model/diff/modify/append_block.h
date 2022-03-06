// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "../block_diff.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::modify {

struct append_block_t final : block_diff_t {

    append_block_t(const file_info_t &file, size_t block_index, std::string data) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(block_visitor_t &) const noexcept override;

    std::string data;
};

} // namespace syncspirit::model::diff::modify
