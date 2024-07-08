#include "augmentation.h"
#include "tree_item.h"

namespace syncspirit::fltk {

augmentation_t::augmentation_t(tree_item_t *owner_) : owner{owner_} {}

void augmentation_t::on_update() noexcept {
    if (owner) {
        owner->on_update();
    }
}

void augmentation_t::on_delete() noexcept {
    if (owner) {
        owner->on_delete();
    }
}

void augmentation_t::release_onwer() noexcept { owner = nullptr; }

tree_item_t *augmentation_t::get_owner() noexcept { return owner; }

} // namespace syncspirit::fltk
