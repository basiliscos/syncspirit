// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "blocks_availability.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"

using namespace syncspirit::model::diff::local;

blocks_availability_t::blocks_availability_t(const file_info_t &file, valid_blocks_map_t valid_blocks_map_) noexcept
    : block_diff_t{file}, valid_blocks_map{std::move(valid_blocks_map_)} {
    LOG_DEBUG(log, "blocks_availability_t, file = {}, valid blocks = {}", file.get_name(), valid_blocks_map.size());
    assert(!file.is_locally_available());
    assert(file.get_blocks().size() == valid_blocks_map.size());
}

auto blocks_availability_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device_id(device_id);
    auto file = folder_info->get_file_infos().by_name(file_name);
    for (size_t i = 0; i < valid_blocks_map.size(); ++i) {
        if (valid_blocks_map[i]) {
            file->mark_local_available(i);
        }
    }
    return applicator_t::apply_sibling(cluster, controller);
}

auto blocks_availability_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting blocks_availability_t");
    return visitor(*this, custom);
}
