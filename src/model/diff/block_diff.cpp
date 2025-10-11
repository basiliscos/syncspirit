// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "block_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

block_diff_t::block_diff_t(const block_diff_t &source) noexcept
    : file_name(source.file_name), folder_id{source.folder_id}, device_id{source.device_id},
      block_hash{source.block_hash}, block_index{source.block_index} {}

block_diff_t::block_diff_t(std::string file_name_,  std::string folder_id_, utils::bytes_t device_id_,
             utils::bytes_t block_hash_, std::uint32_t block_index_) noexcept:
    file_name{std::move(file_name_)}, folder_id{std::move(folder_id_)}, device_id{std::move(device_id_)},
    block_hash{std::move(block_hash_)}, block_index{block_index_}
{
}


block_diff_t::block_diff_t(const file_info_t &file, const folder_info_t &fi, uint32_t block_index_) noexcept
    : file_name{file.get_name()->get_full_name()}, block_index{block_index_} {
    folder_id = fi.get_folder()->get_id();
    auto device = fi.get_device();
    device_id = device->device_id().get_sha256();
    block_hash = file.iterate_blocks(block_index_).next()->get_hash();
}
