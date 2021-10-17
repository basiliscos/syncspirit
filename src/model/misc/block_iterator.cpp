#include "block_iterator.h"
#include "../file_info.h"
#include <cassert>

using namespace syncspirit::model;

blocks_interator_t::blocks_interator_t() noexcept : i{0}, source{nullptr}, target{nullptr} {}
blocks_interator_t::blocks_interator_t(file_info_t &source_, file_info_t &target_) noexcept
    : i{0}, source{&source_}, target{&target_} {
    auto &sb = source->get_blocks();
    auto &tb = target->get_blocks();
    assert(sb.size() == tb.size() && "number of blocks should match");

    size_t j = 0;
    auto max = tb.size();
    while (j < max && tb[j] && *tb[j] == *sb[j]) {
        ++j;
    }
    i = j;
    prepare();
}

void blocks_interator_t::prepare() noexcept {
    if (source) {
        if (i >= source->blocks.size()) {
            source = nullptr;
        }
    }
}

void blocks_interator_t::reset() noexcept { source = nullptr; }

file_block_t blocks_interator_t::next() noexcept {
    assert(source);
    auto &sb = source->get_blocks();
    auto &tb = target->get_blocks();
    while (i < tb.size() && tb[i] && tb[i] == sb[i]) {
        ++i;
    }

    auto index = i++;
    auto file = source;
    prepare();
    return {sb[index].get(), file, index};
}
