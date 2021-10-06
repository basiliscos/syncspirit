#pragma once

#include <vector>
#include "block_info.h"

namespace syncspirit::model {

struct folder_info_t;

struct blocks_interator_t {
    using blocks_t = std::vector<block_info_ptr_t>;

    blocks_interator_t() noexcept;
    blocks_interator_t(file_info_t &file) noexcept;

    template <typename T> blocks_interator_t &operator=(T &other) noexcept {
        file = other.file;
        i = other.i;
        return *this;
    }

    inline operator bool() noexcept { return file != nullptr; }

    file_block_t next() noexcept;
    void reset() noexcept;

  private:
    void prepare() noexcept;
    size_t i = 0;
    file_info_t *file;
};

} // namespace syncspirit::model
