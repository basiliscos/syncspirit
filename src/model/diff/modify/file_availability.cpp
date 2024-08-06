// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "file_availability.h"
#include "../cluster_visitor.h"
#include "model/misc/version_utils.h"

using namespace syncspirit::model::diff::modify;

file_availability_t::file_availability_t(file_info_ptr_t file_) noexcept : file{file_} {
    folder_id = file_->get_folder_info()->get_folder()->get_id();
    version = file->get_version();
}

auto file_availability_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    if (folder) {
        auto folder_info = folder->get_folder_infos().by_device(*cluster.get_device());
        auto f = folder_info->get_file_infos().by_name(file->get_name());
        if (f && compare(version, f->get_version()) == version_relation_t::identity) {
            auto &blocks = f->get_blocks();
            for (size_t i = 0; i < blocks.size(); ++i) {
                if (!f->is_locally_available(i)) {
                    f->mark_local_available(i);
                }
            }
        }
    }
    return applicator_t::apply_sibling(cluster);
}

auto file_availability_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting file_availability_t, folder = {}, file = {}", folder_id, file->get_name());
    return visitor(*this, custom);
}
