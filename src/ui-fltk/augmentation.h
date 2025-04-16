// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "model/misc/augmentation.h"
#include "model/file_info.h"
#include "entry_stats.h"
#include <set>

namespace syncspirit::fltk {

struct tree_item_t;
struct dynamic_item_t;

struct augmentation_base_t : model::augmentation_t {
    virtual tree_item_t *get_owner() noexcept = 0;
    virtual void release_owner() noexcept = 0;
};

using augmentation_ptr_t = model::intrusive_ptr_t<augmentation_base_t>;

struct augmentation_t : augmentation_base_t {
    augmentation_t(tree_item_t *owner);

    void on_update() noexcept override;
    void on_delete() noexcept override;
    void release_owner() noexcept override;
    tree_item_t *get_owner() noexcept override;

  protected:
    tree_item_t *owner;
};

} // namespace syncspirit::fltk
