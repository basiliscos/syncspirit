// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "cluster_diff.h"
#include "apply_controller.h"
#include "model/cluster.h"

using namespace syncspirit::model::diff;

cluster_diff_t::cluster_diff_t() { log = get_log(); }

auto cluster_diff_t::get_log() noexcept -> utils::logger_t { return utils::get_logger("model.diff"); }

cluster_diff_t *cluster_diff_t::assign_sibling(cluster_diff_t *sibling_) noexcept {
    assert(!sibling);
    sibling = sibling_;

    auto n = sibling_;
    while (n->sibling) {
        n = n->sibling.get();
    }
    return n;
}

cluster_diff_t *cluster_diff_t::assign_child(cluster_diff_ptr_t child_) noexcept {
    assert(!child);
    child = std::move(child_);
    return child.get();
}

auto cluster_diff_t::apply(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    auto r = apply_forward(controller, custom);
    if (!r) {
        controller.get_cluster().mark_tainted();
    }
    return r;
}

auto cluster_diff_t::apply_forward(apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    return controller.apply(*this, custom);
}

auto cluster_diff_t::cluster_diff_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    auto r = outcome::result<void>{outcome::success()};
    if (this->child) {
        r = this->child->apply(controller, custom);
    }
    if (r && this->sibling) {
        r = this->sibling->apply(controller, custom);
    }
    return r;
}

auto cluster_diff_t::apply_child(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    return child ? child->apply(controller, custom) : outcome::success();
}

auto cluster_diff_t::apply_sibling(apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    return sibling ? sibling->apply(controller, custom) : outcome::success();
}

auto cluster_diff_t::visit_next(visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    auto r = outcome::result<void>{outcome::success()};
    if (child) {
        r = child->visit(visitor, custom);
    }
    if (r && sibling) {
        r = sibling->visit(visitor, custom);
    }
    return r;
}

auto cluster_diff_t::visit(visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visit_next(visitor, custom);
}
