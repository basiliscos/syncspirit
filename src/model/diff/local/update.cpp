// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "update.h"
#include "model/diff/cluster_visitor.h"
#include "model/diff/modify/add_blocks.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/cluster.h"
#include "model/misc/version_utils.h"
#include "model/misc/orphaned_blocks.h"

#include <cassert>

using namespace syncspirit::model::diff::local;

update_t::update_t(const model::cluster_t &cluster, sequencer_t &sequencer, std::string_view folder_id_,
                   proto::FileInfo file_) noexcept
    : folder_id{folder_id_}, file{std::move(file_)}, already_exists{false} {

    auto orphaned_candidates = orphaned_blocks_t{};
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(*cluster.get_device());
    auto prev_file = folder_info->get_file_infos().by_name(file.name());
    already_exists = (bool)prev_file;

    auto orphaned_blocks = orphaned_blocks_t::set_t{};

    if (prev_file) {
        auto orphaned_candidates = orphaned_blocks_t{};
        orphaned_candidates.record(*prev_file);
        orphaned_blocks = orphaned_candidates.deduce();
        assign(uuid, prev_file->get_uuid());
    } else {
        uuid = sequencer.next_uuid();
    }

    auto &blocks_map = cluster.get_blocks();
    auto new_blocks = modify::add_blocks_t::blocks_t{};
    for (int i = 0; i < file.blocks_size(); ++i) {
        auto &block = file.blocks(i);
        auto strict_hash = block_info_t::make_strict_hash(block.hash());
        auto existing_block = blocks_map.get(strict_hash.get_hash());
        if (!existing_block) {
            new_blocks.push_back(block);
        } else {
            auto it = orphaned_blocks.find(strict_hash.get_key());
            if (it != orphaned_blocks.end()) {
                orphaned_blocks.erase(it);
            }
        }
    }

    auto current = (cluster_diff_t *){};
    if (!new_blocks.empty()) {
        current = assign_child(new modify::add_blocks_t(std::move(new_blocks)));
    }

    if (!orphaned_blocks.empty()) {
        auto diff = cluster_diff_ptr_t{};
        diff = new modify::remove_blocks_t(std::move(orphaned_blocks));
        if (current) {
            current->assign_sibling(diff.get());
        } else {
            assign_child(diff);
        }
    }
}

auto update_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto r = applicator_t::apply_child(cluster);
    if (!r) {
        return r;
    }
    LOG_TRACE(log, "update_t, folder: {}, file: {}", folder_id, file.name());

    auto folder = cluster.get_folders().by_id(folder_id);
    auto &device = *cluster.get_device();
    auto folder_info = folder->get_folder_infos().by_device(device);
    auto prev_file = folder_info->get_file_infos().by_name(file.name());

    auto file = this->file;
    auto seq = folder_info->get_max_sequence() + 1;
    file.set_sequence(seq);
    auto version_ptr = file.mutable_version();

    if (prev_file) {
        *version_ptr = prev_file->get_version();
    }
    record_update(*version_ptr, device);

    auto opt = file_info_t::create(uuid, file, folder_info);
    if (!opt) {
        return opt.assume_error();
    }
    auto file_info = std::move(opt.value());

    auto &blocks_map = cluster.get_blocks();
    assert(!(file.blocks_size() && file.deleted()));
    for (int i = 0; i < file.blocks_size(); ++i) {
        auto &block = file.blocks(i);
        auto strict_hash = block_info_t::make_strict_hash(block.hash());
        auto block_info = blocks_map.get(strict_hash.get_hash());
        assert(block_info);
        file_info->assign_block(block_info, i);
        file_info->mark_local_available(i);
    }
    file_info->mark_local();
    if (prev_file) {
        folder_info->get_file_infos().remove(prev_file);
        prev_file->update(*file_info);
        file_info = std::move(prev_file);
    }

    folder_info->add_strict(file_info);
    file_info->notify_update();

    return applicator_t::apply_sibling(cluster);
}

auto update_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting update_t, folder = {}, file = {}", folder_id, file.name());
    return visitor(*this, custom);
}
