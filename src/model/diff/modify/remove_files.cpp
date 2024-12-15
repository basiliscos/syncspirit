// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "remove_files.h"
#include "remove_blocks.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

remove_files_t::remove_files_t(const device_t &device, const file_infos_map_t &files,
                               orphaned_blocks_t *orphaned_blocks_) noexcept
    : device_id{device.device_id().get_sha256()} {
    keys.reserve(files.size());
    folder_ids.reserve(files.size());
    auto local_orphaned_blocks = orphaned_blocks_t();
    auto &orphaned_blocks = orphaned_blocks_ ? *orphaned_blocks_ : local_orphaned_blocks;
    for (auto &it : files) {
        auto &file = it.item;
        orphaned_blocks.record(*file);
        folder_ids.push_back(std::string(file->get_folder_info()->get_folder()->get_id()));
        keys.push_back(std::string(file->get_key()));
    }
    if (!orphaned_blocks_) {
        auto block_keys = local_orphaned_blocks.deduce();
        if (block_keys.size()) {
            assign_child(new remove_blocks_t(std::move(block_keys)));
        }
    }
    LOG_DEBUG(log, "remove_files_t, device = {}, files count = {}", device.device_id(), files.size());
}

auto remove_files_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto r = applicator_t::apply_child(cluster);
    if (!r) {
        return r;
    }
    auto &folders = cluster.get_folders();
    for (size_t i = 0; i < folder_ids.size(); ++i) {
        auto folder = cluster.get_folders().by_id(folder_ids[i]);
        auto &folder_infos = folder->get_folder_infos();
        auto folder_info = folder_infos.by_device_id(device_id);
        auto &file_infos = folder_info->get_file_infos();
        auto decomposed = file_info_t::decompose_key(keys[i]);
        auto file = file_infos.get(decomposed.file_id);
        file_infos.remove(file);
    }
    return applicator_t::apply_sibling(cluster);
}

auto remove_files_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_files_t");
    return visitor(*this, custom);
}
