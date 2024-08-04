// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "unshare_folder.h"
#include "remove_blocks.h"
#include "remove_files.h"
#include "remove_folder_infos.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"

using namespace syncspirit::model::diff::modify;

unshare_folder_t::unshare_folder_t(const model::cluster_t &cluster, const model::folder_info_t &folder_info,
                                   orphaned_blocks_t *orphaned_blocks_) noexcept
    : parent_t() {

    auto &peer = *folder_info.get_device();
    peer_id = peer.device_id().get_sha256();
    auto &blocks = cluster.get_blocks();

    auto &file_infos = folder_info.get_file_infos();
    auto removed_files = file_infos_map_t();
    auto local_orphaned_blocks = orphaned_blocks_t();
    auto &orphaned_blocks = orphaned_blocks_ ? *orphaned_blocks_ : local_orphaned_blocks;
    for (auto &fi : file_infos) {
        removed_files.put(fi.item);
        orphaned_blocks.record(*fi.item);
    }

    if (removed_files.size()) {
        diffs.emplace_back(new modify::remove_files_t(peer, removed_files));
    }
    if (!orphaned_blocks_) {
        auto block_keys = local_orphaned_blocks.deduce();
        if (block_keys.size()) {
            diffs.emplace_back(cluster_diff_ptr_t(new remove_blocks_t(std::move(block_keys))));
        }
    }
    auto removed_folders = remove_folder_infos_t::unique_keys_t();
    removed_folders.emplace(folder_info.get_key());
    diffs.emplace_back(new modify::remove_folder_infos_t(std::move(removed_folders)));
}

auto unshare_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applyging unshare_folder_t");
    return parent_t::apply_impl(cluster);
}

auto unshare_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting unshare_folder_t");
    return visitor(*this, custom);
}
