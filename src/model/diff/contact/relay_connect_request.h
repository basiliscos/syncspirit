// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <boost/asio.hpp>
#include "../cluster_diff.h"
#include "model/cluster.h"
#include "model/device_id.h"

namespace syncspirit::model::diff::contact {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

struct SYNCSPIRIT_API relay_connect_request_t final : cluster_diff_t {
    relay_connect_request_t(model::device_id_t peer, utils::bytes_t session_key, tcp::endpoint relay) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    model::device_id_t peer;
    utils::bytes_t session_key;
    tcp::endpoint relay;
};

} // namespace syncspirit::model::diff::contact
