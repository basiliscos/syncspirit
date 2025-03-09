// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "rx.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::peer;

rx_t::rx_t(utils::bytes_view_t sha256, std::size_t data_size_) noexcept
    : peer_id{sha256.begin(), sha256.end()}, data_size{data_size_} {}

auto rx_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept -> outcome::result<void> {
    auto peer = cluster.get_devices().by_sha256(peer_id);
    if (peer) {
        peer->set_rx_bytes(peer->get_rx_bytes() + data_size);
        peer->notify_update();
    }

    auto self = cluster.get_device();
    self->set_rx_bytes(self->get_rx_bytes() + data_size);
    self->notify_update();

    return applicator_t::apply_sibling(cluster, controller);
}

auto rx_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    // LOG_TRACE(log, "visiting rx_t");
    return visitor(*this, custom);
}
