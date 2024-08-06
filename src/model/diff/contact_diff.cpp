// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "contact_diff.h"
#include "../cluster.h"

using namespace syncspirit::model::diff;

#if 0
auto contact_diff_t::visit(contact_visitor_t &, void *) const noexcept -> outcome::result<void> {
    return outcome::success();
}

contact_diff_t *contact_diff_t::assign_sibling(contact_diff_t *sibling_) noexcept {
    assert(!sibling);
    sibling = sibling_;

    auto n = sibling_;
    while (n->sibling) {
        n = n->sibling.get();
    }
    return n;
}

void contact_diff_t::assign_child(contact_diff_ptr_t child_) noexcept {
    assert(!child);
    child = std::move(child_);
}

auto contact_diff_t::visit(contact_visitor_t &visitor, void *data) const noexcept -> outcome::result<void> {
    auto r = outcome::result<void>{outcome::success()};
    if (child) {
        r = child->visit(visitor, data);
    }
    if (r && sibling) {
        r = sibling->visit(visitor, data);
    }
    return r;
}
#endif
