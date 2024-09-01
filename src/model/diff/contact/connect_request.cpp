// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "connect_request.h"
#include "../cluster_visitor.h"

using namespace syncspirit::model::diff::contact;

connect_request_t::connect_request_t(tcp::socket sock_, const tcp::endpoint &remote_) noexcept
    : sock{new tcp::socket(std::move(sock_))}, remote{remote_} {}

auto connect_request_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    return applicator_t::apply_sibling(cluster);
}

auto connect_request_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting connect_request_t");
    return visitor(*this, custom);
}
