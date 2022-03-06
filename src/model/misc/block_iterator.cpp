// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

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
        auto &b = *sb[i];
        if (!b.local_file() && !b.is_locked()) {
            break;
        }
        ++i;
    }
    prepare();
}

void blocks_iterator_t::prepare() noexcept {
    if (source) {
        if (i >= source->blocks.size()) {
            source = nullptr;
        }
    }
}

void blocks_iterator_t::reset() noexcept { source.reset(); }

file_block_t blocks_iterator_t::next() noexcept {
    assert(source);
    auto src = source.get();
    auto &sb = src->get_blocks();
    auto idx = i++;
    advance();
    return {sb[idx].get(), src, idx};
}
