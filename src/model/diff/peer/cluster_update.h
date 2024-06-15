// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "proto/bep_support.h"
#include "model/diff/aggregate.h"
#include "model/device.h"

namespace syncspirit::model::diff::peer {

struct SYNCSPIRIT_API cluster_update_t final : aggregate_t {
    using message_t = proto::ClusterConfig;

    static outcome::result<cluster_diff_ptr_t> create(const cluster_t &cluster, const model::device_t &source,
                                                      const message_t &message) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string peer_id;

  private:
    cluster_update_t(const model::device_t &source, aggregate_t::diffs_t diffs) noexcept;
};

} // namespace syncspirit::model::diff::peer
