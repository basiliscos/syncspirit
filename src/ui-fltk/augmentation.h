#pragma once

#include "model/misc/augmentation.hpp"

namespace syncspirit::fltk {

struct tree_item_t;

struct augmentation_t : model::augmentation_t {
    augmentation_t(tree_item_t *owner);

    void on_update() noexcept override;
    void on_delete() noexcept override;
    void release_onwer() noexcept;

    tree_item_t *get_owner() noexcept;

  private:
    tree_item_t *owner;
};

using augmentation_ptr_t = model::intrusive_ptr_t<augmentation_t>;

} // namespace syncspirit::fltk
