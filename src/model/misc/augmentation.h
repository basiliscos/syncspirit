// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "arc.hpp"
#include "syncspirit-export.h"

#include <cstdint>

namespace syncspirit::model {

struct SYNCSPIRIT_API ref_countable_base_t {
    inline ref_countable_base_t() : counter{0} {}
    inline ref_countable_base_t(ref_countable_base_t &) : counter{0} {}
    inline virtual ~ref_countable_base_t() = default;
    inline std::uint32_t use_count() const noexcept { return counter; }

    mutable std::uint32_t counter;
};

struct SYNCSPIRIT_API ref_countable_t : virtual ref_countable_base_t {};

inline void intrusive_ptr_add_ref(const ref_countable_base_t *ptr) noexcept { ++ptr->counter; }

inline void intrusive_ptr_release(const ref_countable_base_t *ptr) noexcept {
    if (--ptr->counter == 0) {
        delete ptr;
    }
}

struct SYNCSPIRIT_API augmentation_t : ref_countable_t {
    augmentation_t() = default;
    inline virtual void on_update() noexcept {};
    inline virtual void on_delete() noexcept {};
};

using augmentation_ptr_t = intrusive_ptr_t<augmentation_t>;

struct SYNCSPIRIT_API augmentable_t : ref_countable_t {
    augmentable_t() = default;
    augmentable_t(const augmentable_t &) = delete;
    ~augmentable_t();

    void set_augmentation(augmentation_t &value) noexcept;
    void set_augmentation(augmentation_ptr_t value) noexcept;

    augmentation_ptr_t &get_augmentation() noexcept;

    void notify_update() noexcept;

  protected:
    augmentation_ptr_t extension;
};

} // namespace syncspirit::model
