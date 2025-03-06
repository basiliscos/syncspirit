// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "remove_folder.h"
#include "unshare_folder.h"
#include "remove_blocks.h"
#include "model/cluster.h"
#include "add_pending_folders.h"
#include "model/misc/orphaned_blocks.h"
#include "model/diff/cluster_visitor.h"
#include "proto/proto-helpers-db.h"

using namespace syncspirit::model::diff::modify;

remove_folder_t::remove_folder_t(const model::cluster_t &cluster, model::sequencer_t &sequencer,
                                 const model::folder_t &folder) noexcept
    : folder_id(folder.get_id()) {
    folder_key = folder.get_key();
    auto orphaned_blocks = orphaned_blocks_t();
    auto &folder_infos = folder.get_folder_infos();
    auto current = (cluster_diff_t *){nullptr};
    auto self = cluster.get_device();
    auto pending_folders = add_pending_folders_t::container_t();
    auto assign = [&](cluster_diff_t *next) {
        if (current) {
            current = current->assign_sibling(next);
        } else {
            current = assign_child(next);
        }
    };

    for (auto &it : folder_infos) {
        auto &fi = *it.item;
        auto d = fi.get_device();
        assign(new unshare_folder_t(cluster, *it.item, &orphaned_blocks));
        if (d != self && fi.get_index()) {
            auto db = db::PendingFolder();
            auto &db_fi = db::get_folder_info(db);
            auto &db_f = db::get_folder(db);
            db::set_index_id(db_fi, fi.get_index());
            db::set_max_sequence(db_fi, fi.get_max_sequence());
            db::set_id(db_f, folder.get_id());
            db::set_label(db_f, folder.get_label());
            db::set_read_only(db_f, folder.is_read_only());
            db::set_ignore_permissions(db_f, folder.are_permissions_ignored());
            db::set_ignore_delete(db_f, folder.is_deletion_ignored());
            db::set_disable_temp_indexes(db_f, folder.are_temp_indixes_disabled());
            db::set_paused(db_f, folder.is_paused());
            db::set_scheduled(db_f, folder.is_scheduled());
            db::set_path(db_f, folder.get_path().string());
            db::set_folder_type(db_f, folder.get_folder_type());
            db::set_pull_order(db_f, folder.get_pull_order());

            auto sha256 = d->device_id().get_sha256();
            auto peer_id = utils::bytes_t{sha256.begin(), sha256.end()};
            auto item = add_pending_folders_t::item_t{std::move(db), std::move(peer_id), sequencer.next_uuid()};
            pending_folders.emplace_back(std::move(item));
        }
    }

    auto block_keys = orphaned_blocks.deduce();
    if (block_keys.size()) {
        assign(new remove_blocks_t(std::move(block_keys)));
    }
    if (pending_folders.size()) {
        assign(new add_pending_folders_t(std::move(pending_folders)));
    }

    LOG_DEBUG(log, "remove_folder_t, folder_id = {}", folder_id);
}

auto remove_folder_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    LOG_TRACE(log, "applying remove_folder_t (folder id = {})", folder_id);
    auto r = applicator_t::apply_child(cluster, controller);
    if (!r) {
        return r;
    }

    auto &folders = cluster.get_folders();
    auto folder = folders.by_id(folder_id);
    folder->mark_suspended(true); // aka deleted object marker
    folders.remove(folder);

    return applicator_t::apply_sibling(cluster, controller);
}

auto remove_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting unshare_folder_t (folder id = {})", folder_id);
    return visitor(*this, custom);
}
