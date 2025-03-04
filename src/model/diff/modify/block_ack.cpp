// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "block_ack.h"
#include "model/file_info.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

block_ack_t::block_ack_t(const block_transaction_t &txn) noexcept : parent_t(txn) {
    LOG_DEBUG(log, "block_ack_t, file = {}, folder = {}, block = {}", file_name, folder_id, block_index);
}

auto block_ack_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    bool success = false;
    if (folder) {
        auto folder_info = folder->get_folder_infos().by_device_id(device_id);
        if (folder_info) {
            auto file = folder_info->get_file_infos().by_name(file_name);
            if (file) {
                LOG_TRACE(log, "block_ack_t, '{}' block # {}", file->get_full_name(), block_index);
                file->mark_local_available(block_index);
                success = true;
            }
        }
    }
    if (!success) {
        LOG_TRACE(log, "block_ack_t failed, folder = '{}', file = '{}', block # {}", folder_id, file_name, block_index);
    }
    return applicator_t::apply_sibling(cluster, controller);
}

auto block_ack_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting block_ack_t");
    return visitor(*this, custom);
}
