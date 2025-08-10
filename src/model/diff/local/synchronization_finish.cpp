// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "synchronization_finish.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::local;

synchronization_finish_t::synchronization_finish_t(std::string_view folder_id_) : folder_id{std::move(folder_id_)} {
    LOG_DEBUG(log, "synchronization_finish_t, folder = {}", folder_id);
}

auto synchronization_finish_t::apply_impl(cluster_t &cluster, apply_controller_t &controller,
                                          void *custom) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    if (folder) {
        folder->adjust_synchronization(-1);
        folder->notify_update();
    }
    auto r = applicator_t::apply_sibling(cluster, controller, custom);
    return r;
}

auto synchronization_finish_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting synchronization_finish_t");
    return visitor(*this, custom);
}
