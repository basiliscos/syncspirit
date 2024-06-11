// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "remove_peer.h"
#include "unshare_folder.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/error_code.h"

using namespace syncspirit::model;
using namespace syncspirit::model::diff;
using namespace syncspirit::model::diff::modify;

static auto make_unshare(const cluster_t &cluster, const device_t &peer) -> aggregate_t::diffs_t {
    aggregate_t::diffs_t r;
    auto &folders = cluster.get_folders();
    for (auto it : folders) {
        auto &f = it.item;
        if (auto fi = f->is_shared_with(peer); fi) {
            r.emplace_back(new unshare_folder_t(cluster, *fi));
        }
    }
    return r;
}

remove_peer_t::remove_peer_t(const cluster_t &cluster, const device_t &peer) noexcept
    : aggregate_t(make_unshare(cluster, peer)), peer_id{peer.device_id().get_sha256()} {}

auto remove_peer_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto peer = cluster.get_devices().by_sha256(peer_id);
    if (!peer) {
        return make_error_code(error_code_t::device_does_not_exist);
    }
    if (*peer == *cluster.get_device()) {
        return make_error_code(error_code_t::cannot_remove_self);
    }

    LOG_TRACE(log, "applyging remove_peer_t (start), for device '{}' ({})", peer->device_id().get_short(),
              peer->get_name());
    auto r = aggregate_t::apply_impl(cluster);
    if (!r) {
        return r;
    }

    cluster.get_devices().remove(peer);
    return outcome::success();
}

auto remove_peer_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_peer_t");
    auto r = aggregate_t::visit(visitor, custom);
    if (!r) {
        return r;
    }
    return visitor(*this, custom);
}
