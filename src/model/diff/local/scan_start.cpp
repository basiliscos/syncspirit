// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "scan_start.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::local;

scan_start_t::scan_start_t(std::string_view folder_id_, std::string_view sub_dir_, const pt::ptime &at_)
    : folder_id{folder_id_}, sub_dir{sub_dir_}, at{at_} {
    LOG_DEBUG(log, "scan_start_t, folder = {}, sub_dir = {}", folder_id, sub_dir);
}

auto scan_start_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    auto &cluster = controller.get_cluster();
    auto folder = cluster.get_folders().by_id(folder_id);
    folder->set_scan_start(at);
    auto r = applicator_t::apply_sibling(controller, custom);

    auto local_device = cluster.get_device();
    auto &local_folder = *folder->get_folder_infos().by_device(*local_device);
    auto &local_files = local_folder.get_file_infos();
    auto updated = false;
    for (auto &f : local_files) {
        auto &local_file = *f;
        if (local_file.is_local()) {
            auto name = local_file.get_name()->get_full_name();
            if (name.size() >= sub_dir.size()) {
                bool match = true;
                for (size_t i = 0; i < sub_dir.size(); ++i) {
                    if (sub_dir[i] != name[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    local_file.mark_local(false);
                    local_file.notify_update();
                    updated = true;
                }
            }
        }
    }

    if (updated) {
        local_folder.notify_update();
        folder->notify_update();
    }
    return r;
}

auto scan_start_t::apply_forward(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    return controller.apply(*this, custom);
}

auto scan_start_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting scan_start_t");
    return visitor(*this, custom);
}
