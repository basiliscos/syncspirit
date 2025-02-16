// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "suspend_folder.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

suspend_folder_t::suspend_folder_t(const model::folder_t &folder) noexcept : folder_id{folder.get_id()} {
    LOG_DEBUG(log, "suspend_folder_t, folder_id = {}", folder_id);
}

auto suspend_folder_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    LOG_TRACE(log, "applying suspend_folder_t");
    auto folder = cluster.get_folders().by_id(folder_id);
    folder->mark_suspended(true);
    return applicator_t::apply_impl(cluster, controller);
}

auto suspend_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting suspend_folder_t");
    return visitor(*this, custom);
}
