// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include "model/cluster.h"
#include "../cluster_diff.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API file_availability_t final : cluster_diff_t {

    file_availability_t(file_info_ptr_t file) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string folder_id;
    model::file_info_ptr_t file;
    proto::Vector version;
};

} // namespace syncspirit::model::diff::modify
