// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "peer_state.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::contact;

auto peer_state_t::create(cluster_t &cluster, std::string_view peer_id_, const r::address_ptr_t &peer_addr_,
                          model::device_state_t state, std::string connection_id_, std::string cert_name_,
                          tcp::endpoint endpoint_, std::string_view client_name_,
                          std::string_view client_version_) noexcept -> cluster_diff_ptr_t {
    auto peer = cluster.get_devices().by_sha256(peer_id_);
    bool update_state = (peer->get_connection_id() == connection_id_) || peer->get_state() < state ||
                        (peer->get_state() == state && peer->get_connection_id() > connection_id_);

    auto diff = cluster_diff_ptr_t{};
    if (update_state) {
        diff = new peer_state_t(cluster, peer_id_, peer_addr_, state, std::move(connection_id_), std::move(cert_name_),
                                std::move(endpoint_), client_name_, client_version_);
    }
    return diff;
}

peer_state_t::peer_state_t(cluster_t &cluster, std::string_view peer_id_, const r::address_ptr_t &peer_addr_,
                           model::device_state_t state_, std::string connection_id_, std::string cert_name_,
                           tcp::endpoint endpoint_, std::string_view client_name_,
                           std::string_view client_version_) noexcept
    : peer_id{peer_id_}, peer_addr{peer_addr_}, cert_name{cert_name_}, endpoint{endpoint_}, client_name{client_name_},
      client_version{client_version_}, state{state_}, connection_id{connection_id_}, has_been_online{false} {
    auto peer = cluster.get_devices().by_sha256(peer_id);
    assert(peer);
    has_been_online = (state == device_state_t::offline) && (peer->get_state() == device_state_t::online);
    LOG_DEBUG(log, "peer_state_t ({}), device = {}, cert = {}, client ({})", (int)state, peer->device_id().get_short(),
              cert_name, client_name, client_version);
}

auto peer_state_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto peer = cluster.get_devices().by_sha256(peer_id);
    bool update_state = (peer->get_connection_id() == connection_id) || peer->get_state() < state ||
                        (peer->get_state() == state && peer->get_connection_id() > connection_id);
    if (update_state) {
        LOG_DEBUG(log, "peer_state_t, applying {}, device = {}", (int)state, peer->device_id().get_short());
        peer->update_state(state, connection_id);
        if (state == device_state_t::online) {
            peer->update_contact(endpoint, client_name, client_version);
        }
        peer->notify_update();

    } else {
        LOG_DEBUG(log, "peer_state_t, ignored {}, device = {}", (int)state, peer->device_id().get_short());
    }
    return applicator_t::apply_impl(cluster, controller);
}

auto peer_state_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
