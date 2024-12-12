// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "file_availability.h"
#include "../cluster_visitor.h"

using namespace syncspirit::model::diff::local;

file_availability_t::file_availability_t(file_info_ptr_t file_) noexcept : file{file_} {
    folder_id = file_->get_folder_info()->get_folder()->get_id();
    version = file->get_version();
}

auto file_availability_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    if (folder) {
        auto folder_info = folder->get_folder_infos().by_device(*cluster.get_device());
        auto f = folder_info->get_file_infos().by_name(file->get_name());
        auto &v_l = version;
        auto &v_c = f->get_version();
        auto version_equal = v_l.counters_size() == v_c.counters_size();
        if (version_equal) {
            for (int i = 0; i < v_l.counters_size() && version_equal; ++i) {
                auto &c_l = v_l.counters(i);
                auto &c_c = v_c.counters(i);
                version_equal = c_l.id() == c_c.id() && c_l.value() == c_c.value();
            }
        }
        if (version_equal) {
            f->mark_local();
            LOG_TRACE(log, "file_availability_t, mark local file = {}, folder = {}, ", file->get_name(), folder_id);
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
