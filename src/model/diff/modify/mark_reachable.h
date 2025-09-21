// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API mark_reachable_t final : cluster_diff_t {

    mark_reachable_t(const model::file_info_t &file, const folder_info_t& fi, bool reachable) noexcept;

    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    utils::bytes_t device_id;
    std::string file_name;
    bool reachable;
};

} // namespace syncspirit::model::diff::modify
