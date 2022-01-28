#include "../cluster.h"
#include "block_iterator.h"
#include <cassert>

using namespace syncspirit::model;

blocks_interator_t::blocks_interator_t(file_info_t &source_) noexcept
    : i{0}, source{&source_} {
    auto &sb = source->get_blocks();

    if (i == sb.size()) {
        source.reset();
        return;
    }
    advance();
}

void blocks_interator_t::advance() noexcept {
    auto &sb = source->get_blocks();
    auto max = sb.size();
    while (i < max && sb[i]) {
        auto& b = *sb[i];
        if (!b.local_file() && !b.is_locked()) {
            break;
        }
        ++i;
    }
    prepare();
}


void blocks_interator_t::prepare() noexcept {
    if (source) {
        if (i >= source->blocks.size()) {
            auto& cluster = source->get_folder_info()->get_folder()->get_cluster();
            auto& map = cluster->block_iterator_map;
            auto src = source;
            source = nullptr;
            map.erase(src);
        }
    }
}

void blocks_interator_t::reset() noexcept { source.reset(); }

file_block_t blocks_interator_t::next() noexcept {
    assert(source);
    auto src = source.get();
    auto &sb = src->get_blocks();
    auto idx = i++;
    advance();
    return {sb[idx].get(), src, idx};
}
