// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "reset_folder_infos.h"
#include "constants.h"
#include "remove_files.h"
#include "remove_blocks.h"
#include "model/diff/diff_assembler.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

reset_folder_infos_t::reset_folder_infos_t(const uuid_folder_infos_map_t &map, orphaned_blocks_t *orphaned_blocks_) {
    LOG_DEBUG(log, "reset_folder_infos_t, count = {}", map.size());
    auto local_orphaned_blocks = orphaned_blocks_t();
    auto &orphaned_blocks = orphaned_blocks_ ? *orphaned_blocks_ : local_orphaned_blocks;

    auto assember = model::diff::diff_assember_t(constants::diffs_batch);
    for (auto &it : map) {
        auto &folder_info = *it.second;
        auto &files_info = folder_info.get_file_infos();
        if (files_info.size()) {
            assember.push_back(new remove_files_t(folder_info, files_info, &orphaned_blocks));
        }
    }
    if (!orphaned_blocks_) {
        auto block_keys = local_orphaned_blocks.deduce();
        if (block_keys.size()) {
            assember.push_back(new remove_blocks_t(std::move(block_keys)));
        }
    }
    if (assember.has_diffs()) {
        assign_child(assember.consume());
    }
}

auto reset_folder_infos_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting reset_folder_infos_t");
    return visitor(*this, custom);
}
