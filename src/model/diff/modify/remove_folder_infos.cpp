// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "remove_folder_infos.h"
#include "remove_files.h"
#include "remove_blocks.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include <algorithm>

using namespace syncspirit::model::diff::modify;

remove_folder_infos_t::remove_folder_infos_t(const folder_infos_map_t &map, orphaned_blocks_t *orphaned_blocks_) {
    auto local_orphaned_blocks = orphaned_blocks_t();
    auto &orphaned_blocks = orphaned_blocks_ ? *orphaned_blocks_ : local_orphaned_blocks;

    auto keys = unique_keys_t();
    auto current = (cluster_diff_t *){nullptr};
    for (auto &it : map) {
        auto &folder_info = *it.item;
        keys.emplace(folder_info.get_key());
        auto &files_info = folder_info.get_file_infos();
        if (files_info.size()) {
            auto diff = cluster_diff_ptr_t{};
            diff = new remove_files_t(*folder_info.get_device(), files_info, &orphaned_blocks);
            current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
        }
    }
    if (!orphaned_blocks_) {
        auto block_keys = local_orphaned_blocks.deduce();
        if (block_keys.size()) {
            auto diff = cluster_diff_ptr_t{};
            diff = new remove_blocks_t(std::move(block_keys));
            current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
        }
    }
    std::copy(keys.begin(), keys.end(), std::back_inserter(this->keys));
}

auto remove_folder_infos_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto r = applicator_t::apply_child(cluster);
    if (!r) {
        return r;
    }
    for (auto &key : keys) {
        auto decomposed = folder_info_t::decompose_key(key);
        auto folder = cluster.get_folders().get(decomposed.folder_key());
        auto &folder_infos = folder->get_folder_infos();
        auto device_key = decomposed.device_key();
        auto folder_info = folder_infos.by_device_key(device_key);
        folder_infos.remove(folder_info);
    }
    return applicator_t::apply_sibling(cluster);
}

auto remove_folder_infos_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_folder_infos_t");
    return visitor(*this, custom);
}
