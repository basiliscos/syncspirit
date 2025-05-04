// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "augmentation.h"

namespace syncspirit::model {

augmentable_t::~augmentable_t() {
    if (extension) {
        extension->on_delete();
    }
}

void augmentable_t::set_augmentation(augmentation_t &value) noexcept { extension = &value; }
void augmentable_t::set_augmentation(augmentation_ptr_t value) noexcept { extension = std::move(value); }

augmentation_ptr_t &augmentable_t::get_augmentation() noexcept { return extension; }

void augmentable_t::notify_update() noexcept {
    if (extension) {
        extension->on_update();
    }
}

} // namespace syncspirit::model
