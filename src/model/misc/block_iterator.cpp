// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "../cluster.h"
#include "block_iterator.h"
#include <cassert>

using namespace syncspirit::model;

blocks_iterator_t::blocks_iterator_t(file_info_t &source_) noexcept : i{0}, source{&source_} {
    auto &sb = source->get_blocks();

    if (i == sb.size()) {
        source.reset();
        return;
    }
    advance();
}

void blocks_iterator_t::advance() noexcept {
    auto &sb = source->get_blocks();
    auto max = sb.size();
    while (i < max && sb[i]) {
        if (!source->is_locally_available(i) && !sb[i]->is_locked()) {
            break;
        }
        ++i;
    }
    prepare();
}

void blocks_iterator_t::prepare() noexcept {
    if (source) {
        bool reset = source->is_unreachable() || (i >= source->blocks.size());
        if (reset) {
            source.reset();
        }
    }
}

void blocks_iterator_t::reset() noexcept { source.reset(); }

file_block_t blocks_iterator_t::next(bool advance_) noexcept {
    assert(source);
    auto src = source.get();
    auto &sb = src->get_blocks();
    auto idx = i;
    if (advance_) {
        ++i;
        advance();
    }
    return {sb[idx].get(), src, idx};
}

file_info_ptr_t blocks_iterator_t::get_source() noexcept { return source; }
