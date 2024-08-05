// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "dial_request.h"
#include "../contact_visitor.h"

using namespace syncspirit::model::diff::contact;

dial_request_t::dial_request_t(model::device_t &peer) noexcept : peer_id{peer.device_id().get_sha256()} {}

auto dial_request_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    return next ? next->apply(cluster) : outcome::success();
}

auto dial_request_t::visit(contact_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting dial_request_t");
    return visitor(*this, custom);
}
