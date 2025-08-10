// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "model/diff/cluster_diff.h"
#include <cstddef>

namespace syncspirit::model::diff::peer {

struct SYNCSPIRIT_API rx_tx_t final : cluster_diff_t {
    rx_tx_t(utils::bytes_view_t sha256, std::size_t rx_size, std::size_t tx_size) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    utils::bytes_t peer_id;
    std::size_t rx_size;
    std::size_t tx_size;
};

} // namespace syncspirit::model::diff::peer
