// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "remove_folder_infos.h"
#include "remove_files.h"
#include "remove_blocks.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "db/prefix.h"

using namespace syncspirit::model::diff::modify;

remove_folder_infos_t::remove_folder_infos_t(const folder_infos_map_t &map, orphaned_blocks_t *orphaned_blocks_) {
    auto local_orphaned_blocks = orphaned_blocks_t();
    auto &orphaned_blocks = orphaned_blocks_ ? *orphaned_blocks_ : local_orphaned_blocks;

    auto keys = unique_keys_t();
    for (auto &it : map) {
        auto &folder_info = *it.item;
        keys.emplace(folder_info.get_key());
        auto &files_info = folder_info.get_file_infos();
        diffs.emplace_back(
            cluster_diff_ptr_t(new remove_files_t(*folder_info.get_device(), files_info, &orphaned_blocks)));
    }
    if (!orphaned_blocks_) {
        auto block_keys = local_orphaned_blocks.deduce();
        if (block_keys.size()) {
            diffs.emplace_back(cluster_diff_ptr_t(new remove_blocks_t(std::move(block_keys))));
        }
    }
}

auto remove_folder_infos_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    for (auto &key : keys) {
        auto decomposed = folder_info_t::decompose_key(key);
        char folder_key_data[folder_t::data_length];
        folder_key_data[0] = (char)(db::prefix::folder);
        std::copy(decomposed.folder_uuid.begin(), decomposed.folder_uuid.end(), folder_key_data + 1);
        auto folder_key = std::string_view(folder_key_data, folder_t::data_length);
        auto folder = cluster.get_folders().get(folder_key);
        auto &folder_infos = folder->get_folder_infos();
        auto folder_info = folder_infos.by_device_id(decomposed.device_id);
        folder_infos.remove(folder_info);
    }
    return outcome::success();
}

auto remove_folder_infos_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_folder_infos_t");
    return visitor(*this, custom);
}
