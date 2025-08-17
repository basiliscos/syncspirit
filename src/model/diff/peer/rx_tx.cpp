// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "rx_tx.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::peer;

rx_tx_t::rx_tx_t(utils::bytes_view_t sha256, std::size_t rx_size_, std::size_t tx_size_) noexcept
    : peer_id{sha256.begin(), sha256.end()}, rx_size{rx_size_}, tx_size{tx_size_} {}

auto rx_tx_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    auto &cluster = controller.get_cluster();
    auto peer = cluster.get_devices().by_sha256(peer_id);
    if (peer) {
        peer->set_rx_bytes(peer->get_rx_bytes() + rx_size);
        peer->set_tx_bytes(peer->get_tx_bytes() + tx_size);
        peer->notify_update();
    }

    auto self = cluster.get_device();
    self->set_rx_bytes(self->get_rx_bytes() + rx_size);
    self->set_tx_bytes(self->get_tx_bytes() + tx_size);
    self->notify_update();

    return applicator_t::apply_sibling(controller, custom);
}

auto rx_tx_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    // LOG_TRACE(log, "visiting rx_tx_t");
    return visitor(*this, custom);
}
