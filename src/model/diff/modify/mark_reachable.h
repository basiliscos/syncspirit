// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/file_info.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API mark_reachable_t final : cluster_diff_t {

    mark_reachable_t(const model::file_info_t &file, bool reachable) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    std::string device_id;
    std::string file_name;
    bool reachable;
};

} // namespace syncspirit::model::diff::modify
