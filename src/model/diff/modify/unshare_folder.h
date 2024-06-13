// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "model/diff/aggregate.h"
#include "model/folder_info.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API unshare_folder_t final : aggregate_t {
    unshare_folder_t(const model::cluster_t &cluster, const model::folder_info_t &folder) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string peer_id;
};

} // namespace syncspirit::model::diff::modify
