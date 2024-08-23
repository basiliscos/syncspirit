// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include "../block_diff.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::local {

struct SYNCSPIRIT_API blocks_availability_t final : block_diff_t {

    blocks_availability_t(const file_info_t &file, size_t last_block_index) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(block_visitor_t &, void *) const noexcept override;

    proto::Vector version;
};

} // namespace syncspirit::model::diff::local
