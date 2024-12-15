// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "reset_folder_infos.h"
#include "remove_files.h"
#include "remove_blocks.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

reset_folder_infos_t::reset_folder_infos_t(const folder_infos_map_t &map, orphaned_blocks_t *orphaned_blocks_) {
    LOG_DEBUG(log, "reset_folder_infos_t, count = {}", map.size());
    auto local_orphaned_blocks = orphaned_blocks_t();
    auto &orphaned_blocks = orphaned_blocks_ ? *orphaned_blocks_ : local_orphaned_blocks;

    auto current = (cluster_diff_t *){nullptr};
    for (auto &it : map) {
        auto &folder_info = *it.item;
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
}

auto reset_folder_infos_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting reset_folder_infos_t");
    return visitor(*this, custom);
}
