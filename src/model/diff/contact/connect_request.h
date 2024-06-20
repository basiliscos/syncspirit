// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include <boost/asio.hpp>
#include <mutex>
#include "../contact_diff.h"
#include "model/cluster.h"

namespace syncspirit::model::diff::contact {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

struct SYNCSPIRIT_API connect_request_t final : contact_diff_t {
    using socket_ptr_t = std::unique_ptr<tcp::socket>;
    using mutex_t = std::mutex;

    connect_request_t(tcp::socket sock, const tcp::endpoint &remote) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(contact_visitor_t &, void *) const noexcept override;

    mutable socket_ptr_t sock;
    tcp::endpoint remote;
    mutable mutex_t mutex;
};

} // namespace syncspirit::model::diff::contact
