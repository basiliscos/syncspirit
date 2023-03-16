// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include <rotor/address.hpp>
#include "../cluster_diff.h"
#include "model/device.h"
#include <boost/asio/ip/tcp.hpp>

namespace syncspirit::model::diff::peer {

namespace r = rotor;
using tcp = boost::asio::ip::tcp;

struct SYNCSPIRIT_API peer_state_t final : cluster_diff_t {

    peer_state_t(cluster_t &cluster, std::string_view peer_id_, const r::address_ptr_t &peer_addr_,
                 model::device_state_t state, std::string cert_name_ = {}, tcp::endpoint endpoint_ = {},
                 std::string_view client_name_ = {}) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *custom) const noexcept override;

    std::string peer_id;
    r::address_ptr_t peer_addr;
    std::string cert_name;
    tcp::endpoint endpoint;
    std::string client_name;
    model::device_state_t state;
    bool known;
};

} // namespace syncspirit::model::diff::peer
