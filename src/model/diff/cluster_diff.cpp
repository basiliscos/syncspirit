// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "cluster_diff.h"
#include "cluster_visitor.h"
#include "../cluster.h"
#include <cassert>

using namespace syncspirit::model::diff;

#if 0
cluster_diff_t::cluster_diff_t(cluster_diff_ptr_t child_, cluster_diff_ptr_t sibling_) noexcept :
    child{std::move(child_)}, sibling{sibling_} {}

cluster_diff_t *cluster_diff_t::assign_sibling(cluster_diff_t *sibling_) noexcept {
    assert(!sibling);
    sibling = sibling_;

    auto n = sibling_;
    while (n->sibling) {
        n = n->sibling.get();
    }
    return n;
}

void cluster_diff_t::assign_child(cluster_diff_ptr_t child_) noexcept {
    assert(!child);
    child = std::move(child_);
}

auto cluster_diff_t::visit(cluster_visitor_t &visitor, void *data) const noexcept -> outcome::result<void> {
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
