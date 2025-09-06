// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "model/cluster.h"
#include "../cluster_diff.h"
#include "../cluster_visitor.h"

namespace syncspirit::model::diff::local {

struct SYNCSPIRIT_API file_availability_t final : cluster_diff_t {

    file_availability_t(file_info_ptr_t file) noexcept;

    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    model::file_info_ptr_t file;
    model::version_t version;
};

} // namespace syncspirit::model::diff::local
