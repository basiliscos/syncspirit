// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "update_remote_views.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit;
using namespace syncspirit::model::diff::peer;

update_remote_views_t::update_remote_views_t(const model::device_t &peer_, container_t container_) noexcept
    : container{std::move(container_)}, peer_id{peer_.device_id().get_sha256()} {
    LOG_DEBUG(log, "update_remote_views_t '{}', {} views", peer_.device_id().get_short(), container.size());
}

auto update_remote_views_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    LOG_TRACE(log, "applying update_remote_views_t");

    auto &cluster = controller.get_cluster();
    auto peer = cluster.get_devices().by_sha256(peer_id);
    auto &views = peer->get_remote_view_map();
    auto &folders = cluster.get_folders();
    auto &devices = cluster.get_devices();
    for (auto &item : container) {
        auto folder = folders.by_id(item.folder_id);
        auto device = devices.by_sha256(item.device_id);
        if (folder && device) {
            views.push(*device, *folder, item.index_id, item.max_sequence);
            LOG_TRACE(log, "'{}' update remote view of folder '{}' to max seq. {}", device->device_id().get_short(),
                      folder->get_id(), item.max_sequence);
        }
    }
    return applicator_t::apply_sibling(controller, custom);
}

auto update_remote_views_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting update_remote_views_t");
    return visitor(*this, custom);
}
