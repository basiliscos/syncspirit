// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include <boost/asio.hpp>
#include "model/cluster.h"
#include "model/diff/cluster_diff.h"

namespace syncspirit::model::diff::contact {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

struct SYNCSPIRIT_API dial_request_t final : cluster_diff_t {
    dial_request_t(model::device_t &peer) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    utils::bytes_t peer_id;
};

} // namespace syncspirit::model::diff::contact
