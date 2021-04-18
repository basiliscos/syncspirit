#pragma once

namespace syncspirit::model {

struct storeable_t {
    bool is_dirty() const noexcept;
    void mark_dirty() noexcept;
    void unmark_dirty() noexcept;

  private:
    bool dirty = false;
};

}; // namespace syncspirit::model
