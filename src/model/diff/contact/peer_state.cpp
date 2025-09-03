// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "peer_state.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::contact;

auto peer_state_t::create(cluster_t &cluster, utils::bytes_view_t peer_id_, const r::address_ptr_t &peer_addr_,
                          const model::device_state_t &state, std::string cert_name_, std::string_view client_name_,
                          std::string_view client_version_) noexcept -> cluster_diff_ptr_t {
    auto diff = cluster_diff_ptr_t{};
    auto peer = cluster.get_devices().by_sha256(peer_id_);
    if (peer) {
        auto &prev_state = peer->get_state();
        if (prev_state < state || prev_state.can_roollback_to(state)) {
            diff = new peer_state_t(cluster, peer_id_, peer_addr_, state, std::move(cert_name_), client_name_,
                                    client_version_);
        }
    }
    return diff;
}

peer_state_t::peer_state_t(cluster_t &cluster, utils::bytes_view_t peer_id_, const r::address_ptr_t &peer_addr_,
                           const model::device_state_t &state_, std::string cert_name_, std::string_view client_name_,
                           std::string_view client_version_) noexcept
    : peer_addr{peer_addr_}, cert_name{cert_name_}, client_name{client_name_}, client_version{client_version_},
      state(state_.clone()), has_been_online{false} {
    peer_id = peer_id_;
    auto peer = cluster.get_devices().by_sha256(peer_id_);
    assert(peer);
    auto was_online = peer->get_state().is_online();
    auto isnt_online = !state.is_online();
    has_been_online = was_online && isnt_online;
    LOG_DEBUG(log, "peer_state_t ({}), device = {}, url = {}, client = {}, cert = {}, client {}",
              (int)state.get_connection_state(), peer->device_id().get_short(), state.get_url(), client_name, cert_name,
              client_version);
}

auto peer_state_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    auto &cluster = controller.get_cluster();
    auto peer = cluster.get_devices().by_sha256(peer_id);
    auto &prev_state = peer->get_state();
    auto conn_state = state.get_connection_state();
    if (prev_state < state || prev_state.can_roollback_to(state)) {
        LOG_DEBUG(log, "peer_state_t, {} -> {}, device = {}, url = {}", (int)prev_state.get_connection_state(),
                  (int)conn_state, peer->device_id().get_short(), state.get_url());
        peer->update_state(state.clone());
        if (state.is_online()) {
            peer->update_contact(client_name, client_version);
        }
        peer->notify_update();
    } else {
        LOG_DEBUG(log, "peer_state_t, ignored {}, device = {}", (int)conn_state, peer->device_id().get_short());
    }
    return applicator_t::apply_impl(controller, custom);
}

auto peer_state_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
