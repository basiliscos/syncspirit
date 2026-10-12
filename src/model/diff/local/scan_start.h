// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#pragma once

#include <string>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "../cluster_diff.h"
#include "utils/path.h"

namespace syncspirit::model::diff::local {

namespace pt = boost::posix_time;

struct SYNCSPIRIT_API scan_start_t final : cluster_diff_t {
    scan_start_t(std::string_view folder_id, utils::path_t path, const pt::ptime &at);
    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> apply_forward(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    utils::path_t path;
    pt::ptime at;
};

} // namespace syncspirit::model::diff::local
