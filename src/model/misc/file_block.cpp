// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "file_block.h"
#include "../block_info.h"
#include "../file_info.h"

namespace syncspirit::model {

file_block_t::file_block_t(const block_info_t *block, file_info_t *file_info_, std::size_t block_index_) noexcept
    : block_info{block}, file_info{file_info_}, block_idx{static_cast<std::uint32_t>(block_index_)} {
    if (block->local_file()) {
        block_idx = block_idx | LOCAL_MASK;
    }
}

bool file_block_t::matches(const block_info_t *block, const file_info_t *file) noexcept {
    return block_info == block && file_info == file;
}

file_block_t &file_block_t::operator=(const file_block_t &other) noexcept {
    block_info = other.block_info;
    file_info = other.file_info;
    block_idx = other.block_idx;
    // local = other.local;
    return *this;
}

file_block_t::operator bool() const noexcept { return (bool)file_info; }

size_t file_block_t::get_offset() const noexcept {
    assert(file_info);
    return file_info->get_block_offset(block_idx & INDEX_MASK);
}

void file_block_t::mark_locally_available(bool value) noexcept {
    if (value) {
        block_idx = block_idx | LOCAL_MASK;
    } else {
        block_idx = block_idx & INDEX_MASK;
    }
}

bool file_block_t::is_locally_available() const noexcept { return block_idx & LOCAL_MASK; }

std::uint32_t file_block_t::block_index() const noexcept { return block_idx & INDEX_MASK; }

} // namespace syncspirit::model
