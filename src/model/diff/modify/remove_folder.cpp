// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "remove_folder.h"
#include "unshare_folder.h"
#include "remove_blocks.h"
#include "model/cluster.h"
#include "model/misc/orphaned_blocks.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

remove_folder_t::remove_folder_t(const model::cluster_t &cluster, const model::folder_t &folder) noexcept
    : folder_id(folder.get_id()), folder_key{folder.get_key()} {

    auto orphaned_blocks = orphaned_blocks_t();
    auto &folder_infos = folder.get_folder_infos();
    auto current = (cluster_diff_t *){nullptr};
    auto assign = [&](cluster_diff_t *next) {
        if (current) {
            current = current->assign_sibling(next);
        } else {
            current = assign_child(next);
            ;
        }
    };

    for (auto &it : folder_infos) {
        auto &fi = *it.item;
        assign(new unshare_folder_t(cluster, *it.item, &orphaned_blocks));
    }

    auto block_keys = orphaned_blocks.deduce();
    if (block_keys.size()) {
        assign(new remove_blocks_t(std::move(block_keys)));
    }
}

auto remove_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applyging remove_folder_t (folder id = {})", folder_id);
    auto r = applicator_t::apply_child(cluster);
    if (!r) {
        return r;
    }

    auto &folders = cluster.get_folders();
    auto folder = folders.by_id(folder_id);
    folders.remove(folder);

    return applicator_t::apply_sibling(cluster);
}

auto remove_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting unshare_folder_t (folder id = {})", folder_id);
    return visitor(*this, custom);
}
