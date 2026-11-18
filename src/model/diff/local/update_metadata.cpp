// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "update_metadata.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::local;

update_metadata_t::update_metadata_t(std::string_view folder_id_, utils::metadata_comparator_t comparator_) noexcept
    : folder_id(std::string(folder_id_)), comparator{std::move(comparator_)} {
    LOG_DEBUG(log, "update_metadata_t, folder: '{}', metadata: {}", folder_id, comparator);
}

auto update_metadata_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    auto &cluster = controller.get_cluster();
    auto folder = cluster.get_folders().by_id(folder_id);
    if (folder) {
        auto &folder_infos = folder->get_folder_infos();
        for (auto &it : folder_infos) {
            auto &fi = *it.item;
            fi.set_metadata_comparator(comparator.clone());
            fi.notify_update();
        }
    }
    return applicator_t::apply_sibling(controller, custom);
}

auto update_metadata_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting update_metadata_t, folder: '{}'", folder_id);
    return visitor(*this, custom);
}
