// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "base_diff.h"
#include <cassert>

namespace syncspirit::model::diff {

namespace tag {

struct cluster {};
struct block {};
struct contact {};

} // namespace tag

template <typename Tag, typename T> struct generic_visitor_t;

template <typename Tag, typename T> struct generic_diff_t : base_diff_t {
    using visitor_t = generic_visitor_t<Tag, T>;
    using ptr_t = boost::intrusive_ptr<T>;

    using applicator_t = generic_diff_t;

    generic_diff_t() noexcept = default;
    virtual ~generic_diff_t() = default;

    virtual outcome::result<void> visit(visitor_t &visitor, void *custom) const noexcept {
        return visit_next(visitor, custom);
    }

    outcome::result<void> visit_next(visitor_t &visitor, void *custom) const noexcept {
        auto r = outcome::result<void>{outcome::success()};
        if (child) {
            r = child->visit(visitor, custom);
        }
        if (r && sibling) {
            r = sibling->visit(visitor, custom);
        }
        return r;
    }

    outcome::result<void> apply_impl(cluster_t &cluster) const noexcept override {
        auto r = outcome::result<void>{outcome::success()};
        if (this->child) {
            r = this->child->apply(cluster);
        }
        if (r && this->sibling) {
            r = this->sibling->apply(cluster);
        }
        return r;
    }

    outcome::result<void> apply_child(cluster_t &cluster) const noexcept {
        return child ? child->apply(cluster) : outcome::success();
    }

    outcome::result<void> apply_sibling(cluster_t &cluster) const noexcept {
        auto r = outcome::result<void>{outcome::success()};
        return sibling ? sibling->apply(cluster) : outcome::success();
    }

    T *assign_sibling(T *sibling_) noexcept {
        assert(!sibling);
        sibling = sibling_;

        auto n = sibling_;
        while (n->sibling) {
            n = n->sibling.get();
        }
        return n;
    }

    T *assign_child(ptr_t child_) noexcept {
        assert(!child);
        child = std::move(child_);
        return child.get();
    }

    ptr_t child;
    ptr_t sibling;
};

} // namespace syncspirit::model::diff
