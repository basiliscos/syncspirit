// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "file_availability.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::local;

file_availability_t::file_availability_t(file_info_ptr_t file_, const folder_info_t &fi) noexcept : file{file_} {
    LOG_DEBUG(log, "file_availability_t, file: {}", *file);
    folder_id = fi.get_folder()->get_id();
    version = file->get_version();
}

auto file_availability_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    auto &cluster = controller.get_cluster();
    auto folder = cluster.get_folders().by_id(folder_id);
    if (folder) {
        auto &folder_info = *folder->get_folder_infos().by_device(*cluster.get_device());
        auto f = folder_info.get_file_infos().by_name(file->get_name()->get_full_name());
        if (f->get_version().identical_to(version)) {
            f->mark_local(true, folder_info);
            LOG_TRACE(log, "file_availability_t, mark local file '{}", *file);
            auto &blocks = f->get_blocks();
            for (size_t i = 0; i < blocks.size(); ++i) {
                if (!f->is_locally_available(i)) {
                    f->mark_local_available(i);
                }
            }
            f->notify_update();
        }
    }
    return applicator_t::apply_sibling(controller, custom);
}

auto file_availability_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting file_availability_t, file: '{}'", *file);
    return visitor(*this, custom);
}
