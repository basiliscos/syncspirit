// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "block_rej.h"
#include "model/diff/cluster_visitor.h"
#include "model/file_info.h"
#include "model/cluster.h"

using namespace syncspirit::model::diff::modify;

block_rej_t::block_rej_t(const block_transaction_t &txn) noexcept : parent_t(txn) {
    LOG_DEBUG(log, "block_rej_t, file = {}, folder = {}, block = {}", file_name, folder_id, block_index);
}

auto block_rej_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device_id(device_id);
    auto file = folder_info->get_file_infos().by_name(file_name);
    LOG_TRACE(log, "block_rej_t, '{}' block # {}", file->get_full_name(), block_index);
    file->mark_local_available(block_index);
    return applicator_t::apply_sibling(cluster, controller);
}

auto block_rej_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting block_rej_t");
    return visitor(*this, custom);
}
