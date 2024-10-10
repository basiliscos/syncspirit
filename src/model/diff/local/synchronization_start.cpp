// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "synchronization_start.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::local;

synchronization_start_t::synchronization_start_t(std::string_view folder_id_) : folder_id{std::move(folder_id_)} {}

auto synchronization_start_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    folder->set_synchronizing(true);
    auto r = applicator_t::apply_sibling(cluster);
    folder->notify_update();
    return r;
}

auto synchronization_start_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting synchronization_start_t");
    return visitor(*this, custom);
}
