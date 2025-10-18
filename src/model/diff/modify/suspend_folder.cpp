// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "suspend_folder.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

suspend_folder_t::suspend_folder_t(const model::folder_t &folder, bool value_, const sys::error_code &ec_) noexcept
    : folder_id{folder.get_id()}, value{value_}, ec{ec_} {
    LOG_DEBUG(log, "suspend_folder_t ({}), folder_id = {}", value, folder_id);
}

auto suspend_folder_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    LOG_TRACE(log, "applying suspend_folder_t");
    auto &cluster = controller.get_cluster();
    auto folder = cluster.get_folders().by_id(folder_id);
    folder->mark_suspended(value, ec);
    return applicator_t::apply_impl(controller, custom);
}

auto suspend_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting suspend_folder_t");
    return visitor(*this, custom);
}
