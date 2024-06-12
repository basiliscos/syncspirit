// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "relay_connect_request.h"
#include "../contact_visitor.h"

using namespace syncspirit::model::diff::modify;

relay_connect_request_t::relay_connect_request_t(model::device_id_t peer_, std::string session_key_,
                                                 tcp::endpoint relay_) noexcept
    : peer{std::move(peer_)}, session_key{std::move(session_key_)}, relay{std::move(relay_)} {}

auto relay_connect_request_t::apply_impl(cluster_t &) const noexcept -> outcome::result<void> {
    return outcome::success();
}

auto relay_connect_request_t::visit(contact_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting relay_connect_request_t");
    return visitor(*this, custom);
}
