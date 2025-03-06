// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "file_availability.h"
#include "../cluster_visitor.h"

using namespace syncspirit::model::diff::local;

file_availability_t::file_availability_t(file_info_ptr_t file_) noexcept : file{file_} {
    LOG_DEBUG(log, "blocks_availability_t, file = {}", file->get_name());
    folder_id = file_->get_folder_info()->get_folder()->get_id();
    version.reset(new version_t(file->get_version()->as_proto()));
}

auto file_availability_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    if (folder) {
        auto folder_info = folder->get_folder_infos().by_device(*cluster.get_device());
        auto f = folder_info->get_file_infos().by_name(file->get_name());
        if (f->get_version()->identical_to(*version)) {
            f->mark_local();
            LOG_TRACE(log, "file_availability_t, mark local file = {}, folder = {}, ", file->get_name(), folder_id);
            auto &blocks = f->get_blocks();
            for (size_t i = 0; i < blocks.size(); ++i) {
                if (!f->is_locally_available(i)) {
                    f->mark_local_available(i);
                }
            }
            f->notify_update();
        }
    }
    return applicator_t::apply_sibling(cluster, controller);
}

auto file_availability_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting file_availability_t, folder = {}, file = {}", folder_id, file->get_name());
    return visitor(*this, custom);
}
