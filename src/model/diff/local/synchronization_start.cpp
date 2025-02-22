// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "synchronization_start.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::local;

synchronization_start_t::synchronization_start_t(std::string_view folder_id_) : folder_id{std::move(folder_id_)} {
    LOG_DEBUG(log, "synchronization_start_t, folder = {}", folder_id);
}

auto synchronization_start_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    if (folder) {
        folder->adjust_synchronization(1);
        folder->notify_update();
    }
    auto r = applicator_t::apply_sibling(cluster, controller);
    return r;
}

auto synchronization_start_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting synchronization_start_t");
    return visitor(*this, custom);
}
