// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "scan_start.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::local;

scan_start_t::scan_start_t(std::string_view folder_id_, const pt::ptime &at_)
    : folder_id{std::move(folder_id_)}, at{at_} {
    LOG_DEBUG(log, "scan_start_t, folder = {}", folder_id);
}

auto scan_start_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    auto &cluster = controller.get_cluster();
    auto folder = cluster.get_folders().by_id(folder_id);
    folder->set_scan_start(at);
    auto r = applicator_t::apply_sibling(controller, custom);

    auto local_device = cluster.get_device();
    auto &local_folder = *folder->get_folder_infos().by_device(*local_device);
    auto &local_files = local_folder.get_file_infos();
    for (auto &f : local_files) {
        auto &local_file = *f;
        if (local_file.is_local() && !local_file.is_deleted()) {
            local_file.mark_local(false);
            local_file.notify_update();
        }
    }

    local_folder.notify_update();
    folder->notify_update();
    return r;
}

auto scan_start_t::apply_forward(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    return controller.apply(*this, custom);
}

auto scan_start_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting scan_start_t");
    return visitor(*this, custom);
}
