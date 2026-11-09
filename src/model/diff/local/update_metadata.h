// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "model/cluster.h"
#include "utils/metadata_comparator.h"
#include "../cluster_diff.h"
#include "../cluster_visitor.h"

namespace syncspirit::model::diff::local {

struct SYNCSPIRIT_API update_metadata_t final : cluster_diff_t {

    update_metadata_t(std::string_view folder_id, utils::metadata_comparator_t) noexcept;

    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    utils::metadata_comparator_t comparator;
};

} // namespace syncspirit::model::diff::local
