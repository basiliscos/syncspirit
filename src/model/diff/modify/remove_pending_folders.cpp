// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "remove_pending_folders.h"

#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

auto remove_pending_folders_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    if (!keys.empty()) {
        LOG_TRACE(log, "applyging remove_pending_folders_t, folders = {}", keys.size());
        auto &map = cluster.get_pending_folders();
        for (auto &key : keys) {
            auto folder = map.by_key(key);
            map.remove(folder);
        }
    }
    return applicator_t::apply_sibling(cluster);
}

auto remove_pending_folders_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_pending_folders_t");
    return visitor(*this, custom);
}
