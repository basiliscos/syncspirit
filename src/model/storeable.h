#pragma once
#include <cstdint>

namespace syncspirit::model {

struct storeable_t {
    bool is_dirty() const noexcept;
    void mark_dirty() noexcept;
    void unmark_dirty() noexcept;

    bool is_deleted() const noexcept;
    void mark_deleted() noexcept;

  private:
    static const constexpr uint32_t F_DIRTY = 1 << 0;
    static const constexpr uint32_t F_DELETED = 1 << 1;
    uint32_t flags = 0;
};

}; // namespace syncspirit::model
