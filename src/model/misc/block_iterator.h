#pragma once

#include <vector>
#include "arc.hpp"
#include "../block_info.h"

namespace syncspirit::model {

struct folder_info_t;
struct file_info_t;

struct blocks_interator_t: arc_base_t<blocks_interator_t> {
    using blocks_t = std::vector<block_info_ptr_t>;

    blocks_interator_t(file_info_t &source) noexcept;

    template <typename T> blocks_interator_t &operator=(T &other) noexcept {
        source = other.source;
        i = other.i;
        return *this;
    }

    inline operator bool() noexcept { return source != nullptr; }

    file_block_t next() noexcept;
    void reset() noexcept;

  private:
    void prepare() noexcept;
    void advance() noexcept;
    size_t i = 0;
    file_info_ptr_t source;
};

using block_iterator_ptr_t = intrusive_ptr_t<blocks_interator_t>;


} // namespace syncspirit::model
