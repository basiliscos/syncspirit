// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "model/file_info.h"
#include "block_iterator.h"
#include <limits>

using namespace syncspirit::model;

static constexpr auto wrong_index = std::numeric_limits<std::uint32_t>::max();

blocks_iterator_t::blocks_iterator_t(file_info_t &source_, const folder_info_t &source_folder_) noexcept
    : source_folder{source_folder_}, source{&source_}, i{0} {
    auto &sb = source->content.file.blocks;
    if (i == sb.size()) {
        i = wrong_index;
        return;
    }
    sequence = source->get_sequence();
    advance();
}

blocks_iterator_t::operator bool() const noexcept { return (i != wrong_index) && (source->get_sequence() == sequence); }

file_info_t *blocks_iterator_t::get_source() noexcept { return source.get(); }

void blocks_iterator_t::advance() noexcept {
    auto &sb = source->content.file.blocks;
    auto max = sb.size();
    while (i < max && sb[i]) {
        if (!source->is_locally_available(i)) {
            break;
        }
        ++i;
    }
    prepare();
}

void blocks_iterator_t::prepare() noexcept {
    if (i != wrong_index) {
        bool reset = source->is_unreachable() || (i >= source->content.file.blocks.size());
        if (reset) {
            i = wrong_index;
        }
    }
}

file_block_t blocks_iterator_t::next() noexcept {
    auto src = source.get();
    auto &sb = src->content.file.blocks;
    auto idx = i;
    ++i;
    advance();
    return {sb[idx].get(), src, idx};
}
