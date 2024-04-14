// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include <boost/asio.hpp>
#include <mutex>
#include "../contact_diff.h"
#include "model/cluster.h"
#include "model/device_id.h"

namespace syncspirit::model::diff::modify {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

struct SYNCSPIRIT_API relay_connect_request_t final : contact_diff_t {
    relay_connect_request_t(model::device_id_t peer, std::string session_key, tcp::endpoint relay) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(contact_visitor_t &, void *) const noexcept override;

    model::device_id_t peer;
    std::string session_key;
    tcp::endpoint relay;
};

} // namespace syncspirit::model::diff::modify
