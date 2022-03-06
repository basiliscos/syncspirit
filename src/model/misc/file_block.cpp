// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "file_block.h"
#include "../block_info.h"
#include "../file_info.h"

namespace syncspirit::model {

file_block_t::file_block_t(block_info_t *block, file_info_t *file_info_, std::size_t block_index_) noexcept
    : block_info{block}, file_info{file_info_}, block_idx{block_index_} {}

bool file_block_t::matches(const block_info_t *block, const file_info_t *file) noexcept {
    return block_info == block && file_info == file;
}

file_block_t &file_block_t::operator=(const file_block_t &other) noexcept {
    block_info = other.block_info;
    file_info = other.file_info;
    block_idx = other.block_idx;
    local = other.local;
    return *this;
}

file_block_t::operator bool() const noexcept { return (bool)file_info; }

size_t file_block_t::get_offset() const noexcept {
    assert(file_info);
    return file_info->get_block_offset(block_idx);
}

} // namespace syncspirit::model
