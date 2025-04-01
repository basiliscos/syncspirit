// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "arc.hpp"
#include <cstdint>

namespace syncspirit::model {

struct ref_countable_base_t {
    inline ref_countable_base_t() : counter{0} {}
    inline ref_countable_base_t(ref_countable_base_t &) : counter{0} {}
    inline virtual ~ref_countable_base_t() = default;
    inline std::uint32_t use_count() const noexcept { return counter; }

    mutable std::uint32_t counter;
};

template <typename T> struct ref_countable_t : virtual ref_countable_base_t {};

template <typename T> inline void intrusive_ptr_add_ref(const ref_countable_t<T> *ptr) noexcept { ++ptr->counter; }

inline void intrusive_ptr_add_ref(const ref_countable_base_t *ptr) noexcept { ++ptr->counter; }

template <typename T> void intrusive_ptr_release(const ref_countable_t<T> *ptr) noexcept {
    if (--ptr->counter == 0) {
        delete dynamic_cast<const T *>(ptr);
    }
}

inline void intrusive_ptr_release(const ref_countable_base_t *ptr) noexcept {
    if (--ptr->counter == 0) {
        delete ptr;
    }
}

struct augmentation_t : ref_countable_t<augmentation_t> {
    augmentation_t() = default;
    inline virtual void on_update() noexcept {};
    inline virtual void on_delete() noexcept {};
};

using augmentation_ptr_t = intrusive_ptr_t<augmentation_t>;

template <typename T> struct augmentable_t : ref_countable_t<T> {

    ~augmentable_t() {
        if (extension) {
            extension->on_delete();
        }
    }

    void set_augmentation(augmentation_t &value) noexcept { extension = &value; }
    void set_augmentation(augmentation_ptr_t value) noexcept { extension = std::move(value); }

    augmentation_ptr_t get_augmentation() noexcept { return extension; }

    void notify_update() noexcept {
        if (extension) {
            extension->on_update();
        }
    }

    augmentation_ptr_t extension;
};

} // namespace syncspirit::model
