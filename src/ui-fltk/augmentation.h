#pragma once

#include "model/misc/augmentation.hpp"

namespace syncspirit::fltk {

struct tree_item_t;

struct augmentation_base_t : model::augmentation_t {
    virtual tree_item_t *get_owner() noexcept = 0;
    virtual void release_onwer() noexcept = 0;
};

using augmentation_ptr_t = model::intrusive_ptr_t<augmentation_base_t>;

struct augmentation_t final : augmentation_base_t {
    augmentation_t(tree_item_t *owner);

    void on_update() noexcept override;
    void on_delete() noexcept override;
    void release_onwer() noexcept override;
    tree_item_t *get_owner() noexcept override;

  private:
    tree_item_t *owner;
};

struct augmentation_proxy_t final : augmentation_base_t {
    augmentation_proxy_t(augmentation_ptr_t backend);

    void on_update() noexcept override;
    void on_delete() noexcept override;
    void release_onwer() noexcept override;
    tree_item_t *get_owner() noexcept override;

  private:
    augmentation_ptr_t backend;
};

} // namespace syncspirit::fltk
