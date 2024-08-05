// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "structs.pb.h"
#include "model/device_id.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API update_peer_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;

    update_peer_t(db::Device db, const model::device_id_t &device_id, const model::cluster_t &) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    db::Device item;
    std::string peer_id;
};

} // namespace syncspirit::model::diff::modify
