// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "local_update.h"
#include "../cluster_visitor.h"
#include "../../cluster.h"
#include "../../misc/version_utils.h"

#include <cassert>

using namespace syncspirit::model::diff::modify;

local_update_t::local_update_t(const model::cluster_t &cluster, std::string_view folder_id_,
                               proto::FileInfo file_) noexcept
    : folder_id{folder_id_}, file{std::move(file_)}, already_exists{false} {
    auto &blocks_map = cluster.get_blocks();
    blocks_t kept_blocks;
    for (int i = 0; i < file.blocks_size(); ++i) {
        auto &block = file.blocks(i);
        auto &hash = block.hash();
        assert(!hash.empty());
        auto existing_block = blocks_map.get(hash);
        if (!existing_block) {
            new_blocks.insert(hash);
        } else {
            kept_blocks.insert(hash);
        }
    }

    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(*cluster.get_device());
    auto prev_file = folder_info->get_file_infos().by_name(file.name());
    already_exists = (bool)prev_file;

    if (prev_file) {
        auto &prev_blocks = prev_file->get_blocks();
        for (auto &b : prev_blocks) {
            auto hash = b->get_hash();
            bool remove = !kept_blocks.count(hash) && b->get_file_blocks().size() == 1;
            if (remove) {
                removed_blocks.insert(std::string(hash));
            }
        }
    }
}

auto local_update_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "local_update_t, folder: {}, file: {}", folder_id, file.name());

    auto folder = cluster.get_folders().by_id(folder_id);
    auto &device = *cluster.get_device();
    auto folder_info = folder->get_folder_infos().by_device(device);
    auto prev_file = folder_info->get_file_infos().by_name(file.name());

    auto file = this->file;
    auto seq = folder_info->get_max_sequence() + 1;
    folder_info->set_max_sequence(seq);
    file.set_sequence(seq);
    auto version_ptr = file.mutable_version();

    auto uuid = uuid_t{};
    if (prev_file) {
        assign(uuid, prev_file->get_uuid());
        *version_ptr = prev_file->get_version();
    } else {
        uuid = cluster.next_uuid();
    }
    record_update(*version_ptr, device);

    auto opt = file_info_t::create(uuid, file, folder_info);
    if (!opt) {
        return opt.assume_error();
    }
    auto file_info = std::move(opt.value());

    auto &blocks_map = cluster.get_blocks();
    for (int i = 0; i < file.blocks_size(); ++i) {
        auto &block = file.blocks(i);
        auto &hash = block.hash();
        auto block_info = block_info_ptr_t{};
        if (new_blocks.find(hash) != new_blocks.end()) {
            auto block_opt = block_info_t::create(block);
            if (!block_opt) {
                return block_opt.assume_error();
            }
            block_info = std::move(block_opt.assume_value());
            blocks_map.put(block_info);
        } else {
            block_info = blocks_map.get(hash);
        }
        assert(block_info);
        file_info->assign_block(block_info, i);
        file_info->mark_local_available(i);
    }
    if (prev_file) {
        for (auto &block_id : removed_blocks) {
            auto block = blocks_map.get(block_id);
            blocks_map.remove(block);
        }
        prev_file->remove_blocks();
    }
    folder_info->add(file_info, true);
    return outcome::success();
}

auto local_update_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting local_update_t, folder = {}, file = {}", folder_id, file.name());
    return visitor(*this, custom);
}
