// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include <boost/asio.hpp>
#include <mutex>
#include "../contact_diff.h"
#include "model/cluster.h"

namespace syncspirit::model::diff::contact {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

struct SYNCSPIRIT_API dial_request_t final : contact_diff_t {
    dial_request_t(model::device_t &peer) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(contact_visitor_t &, void *) const noexcept override;

    std::string peer_id;
};

} // namespace syncspirit::model::diff::contact
