// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "structs.pb.h"
#include "model/misc/uuid.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API upsert_folder_info_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;

    upsert_folder_info_t(const uuid_t &uuid, std::string_view device_id, std::string_view folder_id,
                         std::uint64_t index_id, std::int64_t max_sequence) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    uuid_t uuid;
    std::string device_id;
    std::string folder_id;
    std::uint64_t index_id;
    std::int64_t max_sequence;
};

} // namespace syncspirit::model::diff::modify
