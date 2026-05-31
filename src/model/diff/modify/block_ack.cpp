// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "block_ack.h"
#include "model/file_info.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "utils/bytes.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

block_ack_t::block_ack_t(const block_transaction_t &txn) noexcept : parent_t(txn) {
    LOG_DEBUG(log, "block_ack_t, file: {}, folder: {}, block: #{} (hash: {}) ", file_name, folder_id, block_index,
              block_hash);
}

block_ack_t::block_ack_t(std::string file_name_, std::string folder_id_, utils::bytes_t device_id_,
                         utils::bytes_t block_hash_, std::uint32_t block_index, bool unlock_block_) noexcept
    : parent_t(std::move(file_name_), std::move(folder_id_), std::move(device_id_), std::move(block_hash_),
               block_index),
      unlock_block{unlock_block_} {
    LOG_DEBUG(log, "block_ack_t, file: {}, folder: {}, block: #{} (hash: {}), unlock: {}", file_name, folder_id,
              block_index, block_hash, unlock_block);
}

auto block_ack_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    auto &cluster = controller.get_cluster();
    auto folder = cluster.get_folders().by_id(folder_id);
    bool success = false;
    if (folder) {
        auto folder_info = folder->get_folder_infos().by_device_id(device_id);
        if (folder_info) {
            auto file = folder_info->get_file_infos().by_name(file_name);
            if (file) {
                if (!file->is_locally_available(block_index)) {
                    LOG_TRACE(log, "block_ack_t, '{}' block #{} (hash: {})", file_name, block_index, block_hash);
                    file->mark_local_available(block_index);
                    success = true;
                }
            }
        }
    }
    if (!success) {
        LOG_TRACE(log, "block_ack_t failed, folder = '{}', file = '{}', block: #{} (hash: {})", folder_id, file_name,
                  block_index, block_hash);
    }
    return applicator_t::apply_sibling(controller, custom);
}

auto block_ack_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting block_ack_t");
    return visitor(*this, custom);
}
