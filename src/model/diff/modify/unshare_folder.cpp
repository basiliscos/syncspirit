// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "unshare_folder.h"
#include "remove_blocks.h"
#include "remove_files.h"
#include "remove_folder_infos.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"

using namespace syncspirit::model::diff::modify;

unshare_folder_t::unshare_folder_t(const model::cluster_t &cluster, model::folder_info_t &folder_info,
                                   orphaned_blocks_t *orphaned_blocks_) noexcept
    : parent_t() {

    auto &peer = *folder_info.get_device();
    peer_id = peer.device_id().get_sha256();
    auto &blocks = cluster.get_blocks();
    auto current = (cluster_diff_t *)this;

    auto &file_infos = folder_info.get_file_infos();
    auto local_orphaned_blocks = orphaned_blocks_t();
    auto &orphaned_blocks = orphaned_blocks_ ? *orphaned_blocks_ : local_orphaned_blocks;
    current = current->assign(new remove_files_t(peer, file_infos, &orphaned_blocks));
    auto remove_folders_map = model::folder_infos_map_t{};
    remove_folders_map.put(&folder_info);
    current = current->assign(new remove_folder_infos_t(std::move(remove_folders_map), &orphaned_blocks));

    if (!orphaned_blocks_) {
        auto block_keys = local_orphaned_blocks.deduce();
        if (block_keys.size()) {
            current = current->assign(new remove_blocks_t(std::move(block_keys)));
        }
    }
}

auto unshare_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applyging unshare_folder_t");
    return next ? next->apply(cluster) : outcome::success();
}

auto unshare_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting unshare_folder_t");
    return visitor(*this, custom);
}
