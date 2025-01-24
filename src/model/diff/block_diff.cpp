// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "block_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

block_diff_t::block_diff_t(const block_diff_t &source) noexcept
    : file_name(source.file_name), folder_id{source.folder_id}, device_id{source.device_id},
      block_hash{source.block_hash}, block_index{source.block_index} {}

block_diff_t::block_diff_t(const file_info_t &file, size_t block_index_) noexcept
    : file_name{file.get_name()}, block_index{block_index_} {
    auto fi = file.get_folder_info();
    folder_id = fi->get_folder()->get_id();
    device_id = fi->get_device()->device_id().get_sha256();
    block_hash = file.get_blocks()[block_index_]->get_hash();
}
