// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "peer_state.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::contact;

peer_state_t::peer_state_t(cluster_t &cluster, std::string_view peer_id_, const r::address_ptr_t &peer_addr_,
                           model::device_state_t state_, std::string cert_name_, tcp::endpoint endpoint_,
                           std::string_view client_name_, std::string_view client_version_) noexcept
    : peer_id{peer_id_}, peer_addr{peer_addr_}, cert_name{cert_name_}, endpoint{endpoint_}, client_name{client_name_},
      client_version{client_version_}, state{state_}, has_been_online{false} {
    auto peer = cluster.get_devices().by_sha256(peer_id);
    assert(peer);
    has_been_online = (state == device_state_t::offline) && (peer->get_state() == device_state_t::online);
    LOG_DEBUG(log, "peer_state_t, device = {}, cert = {}, client ({})", peer->device_id().get_short(), cert_name,
              client_name, client_version);
}

auto peer_state_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto peer = cluster.get_devices().by_sha256(peer_id);
    peer->update_state(state);
    if (state == device_state_t::online) {
        peer->update_contact(endpoint, client_name, client_version);
    }
    peer->notify_update();
    return applicator_t::apply_impl(cluster, controller);
}

auto peer_state_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
