// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/misc/uuid.h"
#include "utils/bytes.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API upsert_folder_info_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;

    upsert_folder_info_t(const bu::uuid &uuid, utils::bytes_view_t device_id, std::string_view folder_id,
                         std::uint64_t index_id) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    bu::uuid uuid;
    utils::bytes_t device_id;
    std::string folder_id;
    std::uint64_t index_id;
};

} // namespace syncspirit::model::diff::modify
