// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/device.h"
#include <vector>
#include "structs.pb.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API add_unknown_folders_t final : cluster_diff_t {
    struct item_t {
        db::UnknownFolder db;
        std::string peer_id;
        uuid_t uuid;
    };
    using container_t = std::vector<item_t>;

    add_unknown_folders_t(container_t items) noexcept;
    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    container_t container;
};

} // namespace syncspirit::model::diff::modify
