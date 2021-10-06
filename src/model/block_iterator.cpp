#include "block_iterator.h"
#include "file_info.h"

using namespace syncspirit::model;

blocks_interator_t::blocks_interator_t() noexcept : file{nullptr}, i{0} {}
blocks_interator_t::blocks_interator_t(file_info_t &file_) noexcept : file{&file_}, i{0} {
    prepare();
    if (file) {
        size_t j = 0;
        auto &blocks = file->get_blocks();
        auto &local = file->get_local_blocks();
        auto max = std::min(local.size(), blocks.size());
        while (j < max && local[j] && *local[j] == *blocks[j]) {
            ++j;
        }
        i = j;
    }
}

void blocks_interator_t::prepare() noexcept {
    if (file) {
        if (i >= file->blocks.size()) {
            file = nullptr;
        }
    }
}

void blocks_interator_t::reset() noexcept { file = nullptr; }

file_block_t blocks_interator_t::next() noexcept {
    assert(file);
    auto &blocks = file->blocks;
    auto &local_blocks = file->local_blocks;
    auto b = blocks[i].get();
    block_info_t *result_block = b;
    if (i < local_blocks.size()) {
        auto lb = local_blocks[i].get();
        if (!lb || *lb != *b) {
            result_block = lb;
        }
    }
    auto index = i++;
    auto file_ptr = file;
    prepare();
    return {b, file_ptr, index};
}
