// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "remove_peer.h"
#include "unshare_folder.h"
#include "remove_blocks.h"
#include "remove_unknown_folders.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/error_code.h"

using namespace syncspirit::model;
using namespace syncspirit::model::diff;
using namespace syncspirit::model::diff::modify;

using blocks_t = remove_blocks_t::unique_keys_t;

static auto make_unshare(const cluster_t &cluster, const device_t &peer, blocks_t &removed_blocks)
    -> cluster_aggregate_diff_t::diffs_t {
    cluster_aggregate_diff_t::diffs_t r;
    auto &folders = cluster.get_folders();
    for (auto it : folders) {
        auto &f = it.item;
        if (auto fi = f->is_shared_with(peer); fi) {
            r.emplace_back(new unshare_folder_t(cluster, *fi, &removed_blocks));
        }
    }
    return r;
}

remove_peer_t::remove_peer_t(const cluster_t &cluster, const device_t &peer) noexcept
    : parent_t(), peer_key{peer.get_key()} {

    auto removed_blocks = blocks_t{};

    diffs = make_unshare(cluster, peer, removed_blocks);
    if (removed_blocks.size()) {
        diffs.emplace_back(cluster_diff_ptr_t(new remove_blocks_t(std::move(removed_blocks))));
    }

    auto removed_unknown_folders = remove_unknown_folders_t::unique_keys_t{};
    for (auto &it : cluster.get_unknown_folders()) {
        auto &uf = *it.item;
        if (uf.device_id() == peer.device_id()) {
            removed_unknown_folders.emplace(uf.get_key());
        }
    }
    if (removed_unknown_folders.size()) {
        auto diff = cluster_diff_ptr_t{};
        diff.reset(new remove_unknown_folders_t(std::move(removed_unknown_folders)));
        diffs.emplace_back(diff);
    }
}

auto remove_peer_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto sha256 = get_peer_sha256();
    assert(sha256.size() == device_id_t::digest_length);
    auto peer = cluster.get_devices().by_sha256(sha256);
    if (!peer) {
        return make_error_code(error_code_t::no_such_device);
    }
    if (*peer == *cluster.get_device()) {
        return make_error_code(error_code_t::cannot_remove_self);
    }

    LOG_TRACE(log, "applyging remove_peer_t (start), for device '{}' ({})", peer->device_id().get_short(),
              peer->get_name());
    auto r = parent_t::apply_impl(cluster);
    if (!r) {
        return r;
    }

    cluster.get_devices().remove(peer);
    return outcome::success();
}

std::string_view remove_peer_t::get_peer_sha256() const noexcept { return std::string_view(peer_key).substr(1); }

auto remove_peer_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_peer_t");
    return visitor(*this, custom);
}
