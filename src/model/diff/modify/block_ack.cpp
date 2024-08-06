// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "block_ack.h"
#include "../block_visitor.h"
#include "model/file_info.h"
#include "model/cluster.h"

using namespace syncspirit::model::diff::modify;

block_ack_t::block_ack_t(const block_transaction_t &txn) noexcept : parent_t(txn) {}

auto block_ack_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device_id(device_id);
    auto file = folder_info->get_file_infos().by_name(file_name);
    LOG_TRACE(log, "block_ack_t, '{}' block # {}", file->get_full_name(), block_index);
    file->mark_local_available(block_index);
    return applicator_t::apply_sibling(cluster);
}

auto block_ack_t::visit(block_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting block_ack_t");
    return visitor(*this, custom);
}
