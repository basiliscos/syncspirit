// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "arc.hpp"

namespace syncspirit::model {

struct augmentation_t : arc_base_t<augmentation_t> {
    augmentation_t() = default;
    inline virtual void on_update() noexcept {};
    inline virtual void on_delete() noexcept {};
    virtual ~augmentation_t() = default;
};

using augmentation_ptr_t = intrusive_ptr_t<augmentation_t>;

template <typename T> struct augmentable_t : arc_base_t<T> {

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
