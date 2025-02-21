// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "messages.h"
#include "model/cluster.h"

using namespace syncspirit::net::payload;

block_request_t::block_request_t(const model::file_info_ptr_t &file_, size_t block_index_) noexcept {
    auto fi = file_->get_folder_info();
    folder_id = fi->get_folder()->get_id();
    file_name = file_->get_name();
    sequence = file_->get_sequence();
    block_index = block_index_;
    auto &block = file_->get_blocks()[block_index_];
    auto hash = block->get_hash();
    block_hash = {hash.begin(), hash.end()};
    block_offset = block->get_file_blocks().front().get_offset();
    block_size = block->get_size();
}

auto block_request_t::get_block(model::cluster_t &cluster, model::device_t &peer) noexcept -> model::file_block_t {
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
    auto &file_blocks = blocks[block_index]->get_file_blocks();
    for (auto &fb : file_blocks) {
        if (fb.file() == file) {
            return fb;
        }
    }
    return {};
}
