// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "connect_request.h"
#include "../cluster_visitor.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::contact;

connect_request_t::connect_request_t(tcp::socket sock_, const tcp::endpoint &remote_) noexcept
    : sock{new tcp::socket(std::move(sock_))}, remote{remote_} {
    LOG_DEBUG(log, "connect_request_t, endpoint = {}", remote);
}

auto connect_request_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    return applicator_t::apply_sibling(cluster, controller);
}

auto connect_request_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting connect_request_t");
    return visitor(*this, custom);
}
