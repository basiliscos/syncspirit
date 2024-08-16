// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "add_pending_folders.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

add_unknown_folders_t::add_unknown_folders_t(container_t items) noexcept : container(std::move(items)) {}

auto add_unknown_folders_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &unknown = cluster.get_unknown_folders();
    auto &devices = cluster.get_devices();
    for (auto &item : container) {
        auto peer = devices.by_sha256(item.peer_id);
        auto &db = item.db;
        auto opt = pending_folder_t::create(item.uuid, db, peer->device_id());
        if (!opt) {
            return opt.assume_error();
        }
        unknown.put(std::move(opt.value()));
    }
    return applicator_t::apply_sibling(cluster);
}

auto add_unknown_folders_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting add_unknown_folders_t");
    return visitor(*this, custom);
}
