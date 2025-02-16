// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "remove_peer.h"
#include "unshare_folder.h"
#include "remove_blocks.h"
#include "remove_pending_folders.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/error_code.h"
#include "utils/format.hpp"

using namespace syncspirit::model;
using namespace syncspirit::model::diff;
using namespace syncspirit::model::diff::modify;

using blocks_t = remove_blocks_t::unique_keys_t;

remove_peer_t::remove_peer_t(const cluster_t &cluster, const device_t &peer) noexcept
    : parent_t(), peer_key{peer.get_key()} {
    LOG_DEBUG(log, "remove_peer_t, device = {}", peer.device_id());
    orphaned_blocks_t orphaned_blocks;

    auto removed_pending_folders = remove_pending_folders_t::unique_keys_t{};
    for (auto &it : cluster.get_pending_folders()) {
        auto &uf = *it.item;
        if (uf.device_id() == peer.device_id()) {
            removed_pending_folders.emplace(uf.get_key());
        }
    }

    auto current = (cluster_diff_t *){nullptr};
    if (removed_pending_folders.size()) {
        auto diff = cluster_diff_ptr_t{};
        diff = new remove_pending_folders_t(std::move(removed_pending_folders));
        current = assign_child(diff);
    }

    auto &folders = cluster.get_folders();
    for (auto it : folders) {
        auto &f = it.item;
        if (auto fi = f->is_shared_with(peer); fi) {
            auto diff = cluster_diff_ptr_t{};
            diff = new unshare_folder_t(cluster, *fi, &orphaned_blocks);
            current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
        }
    }

    auto removed_blocks = orphaned_blocks.deduce();
    if (removed_blocks.size()) {
        auto diff = cluster_diff_ptr_t{};
        diff = new remove_blocks_t(std::move(removed_blocks));
        current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
    }
}

auto remove_peer_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto r = applicator_t::apply_child(cluster, controller);
    if (!r) {
        return r;
    }
    auto sha256 = get_peer_sha256();
    assert(sha256.size() == device_id_t::digest_length);
    auto peer = cluster.get_devices().by_sha256(sha256);
    if (!peer) {
        return make_error_code(error_code_t::no_such_device);
    }
    if (*peer == *cluster.get_device()) {
        return make_error_code(error_code_t::cannot_remove_self);
    }

    LOG_TRACE(log, "applying remove_peer_t (start), for device '{}' ({})", peer->device_id().get_short(),
              peer->get_name());
    cluster.get_devices().remove(peer);
    return applicator_t::apply_sibling(cluster, controller);
}

std::string_view remove_peer_t::get_peer_sha256() const noexcept { return std::string_view(peer_key).substr(1); }

auto remove_peer_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_peer_t");
    return visitor(*this, custom);
}
