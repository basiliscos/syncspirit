// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include <vector>
#include "model/device.h"

namespace syncspirit::model::diff::peer {

struct SYNCSPIRIT_API update_remote_views_t final : cluster_diff_t {
    struct item_t {
        std::string folder_id;
        model::device_id_t device_id;
        std::uint64_t index_id;
        std::int64_t max_sequence;
    };
    using container_t = std::vector<item_t>;

    update_remote_views_t(const model::device_t &peer, container_t container) noexcept;
    outcome::result<void> apply_impl(apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

  private:
    container_t container;
    utils::bytes_t peer_id;
};

} // namespace syncspirit::model::diff::peer
