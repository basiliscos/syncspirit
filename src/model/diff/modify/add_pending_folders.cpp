// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "add_pending_folders.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

add_pending_folders_t::add_pending_folders_t(container_t items) noexcept : container(std::move(items)) {
    LOG_DEBUG(log, "add_pending_folders_t, count = {}", container.size());
}

auto add_pending_folders_t::apply_impl(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    auto &pending = cluster.get_pending_folders();
    auto &devices = cluster.get_devices();
    for (auto &item : container) {
        auto peer = devices.by_sha256(item.peer_id);
        auto &db = item.db;
        auto opt = pending_folder_t::create(item.uuid, db, peer->device_id());
        if (!opt) {
            return opt.assume_error();
        }
        pending.put(std::move(opt.value()));
    }
    return applicator_t::apply_sibling(cluster, controller, custom);
}

auto add_pending_folders_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting add_pending_folders_t");
    return visitor(*this, custom);
}
