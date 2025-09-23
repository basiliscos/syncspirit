// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "messages.h"
#include "model/cluster.h"

using namespace syncspirit::net::payload;

block_request_t::block_request_t(const model::file_info_ptr_t &file_, const model::folder_info_t &fi,
                                 size_t block_index_) noexcept {
    folder_id = fi.get_folder()->get_id();
    file_name = file_->get_name()->get_full_name();
    sequence = file_->get_sequence();
    block_index = block_index_;
    auto block = file_->get_blocks()[block_index_].get();
    block_hash = block->get_hash();
    block_size = block->get_size();

    auto it = block->iterate_blocks();
    while (auto fb = it.next()) {
        if (fb->file() == file_.get()) {
            block_offset = fb->get_offset();
            break;
        }
    }
}

auto block_request_t::get_block(model::cluster_t &cluster, model::device_t &peer) noexcept -> block_info_t {
    auto folder = cluster.get_folders().by_id(folder_id);
    if (!folder || folder->is_suspended()) {
        return {};
    }
    auto fi = folder->get_folder_infos().by_device(peer);
    if (!fi) {
        return {};
    }
    auto file = fi->get_file_infos().by_name(file_name);
    if (!file) {
        return {};
    }
    if (file->get_sequence() != sequence) {
        return {};
    }
    auto &blocks = file->get_blocks();
    if (block_index >= blocks.size()) {
        return {};
    }
    auto it = blocks[block_index]->iterate_blocks();
    while (auto fb = it.next()) {
        if (fb->file() == file) {
            return {const_cast<model::file_block_t *>(fb), fi.get()};
        }
    }
    return {};
}
