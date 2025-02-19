// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "dial_request.h"
#include "../cluster_visitor.h"

using namespace syncspirit::model::diff::contact;

dial_request_t::dial_request_t(model::device_t &peer) noexcept {
    auto sha256 = peer.device_id().get_sha256();
    peer_id = {sha256.begin(), sha256.end()};
    LOG_DEBUG(log, "dial_request_t, peer = ", peer.device_id().get_short());
}

auto dial_request_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    return applicator_t::apply_sibling(cluster, controller);
}

auto dial_request_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting dial_request_t");
    return visitor(*this, custom);
}
