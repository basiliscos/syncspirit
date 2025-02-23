// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "relay_connect_request.h"
#include "../cluster_visitor.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::contact;

relay_connect_request_t::relay_connect_request_t(model::device_id_t peer_, utils::bytes_t session_key_,
                                                 tcp::endpoint relay_) noexcept
    : peer{std::move(peer_)}, session_key{std::move(session_key_)}, relay{std::move(relay_)} {
    LOG_DEBUG(log, "relay_connect_request_t, device = {}, relay = {}", peer.get_short(), relay);
}

auto relay_connect_request_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    return applicator_t::apply_sibling(cluster, controller);
}

auto relay_connect_request_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting relay_connect_request_t");
    return visitor(*this, custom);
}
