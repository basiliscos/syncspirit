// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "diff_assembler.h"
#include "model/diff/load/interrupt.h"

#include <cassert>

using namespace syncspirit::model::diff;

diff_assember_t::diff_assember_t(std::uint_fast32_t batch_limit_) noexcept
    : batch_limit{batch_limit_}, left{batch_limit_}, total{0}, next{nullptr} {
    assert(batch_limit > 1);
}

bool diff_assember_t::push_back(cluster_diff_t *diff) noexcept {
    --left;
    ++total;
    auto result = false;
    if (next) {
        next = next->assign_sibling(diff);
    } else {
        root = next = diff;
    }
    if (left == 0) {
        next = next->assign_sibling(new model::diff::load::interrupt_t());
        left = batch_limit;
        result = true;
    }
    return result;
}

bool diff_assember_t::push_front(cluster_diff_t *diff) noexcept {
    --left;
    ++total;
    auto result = false;
    if (next) {
        auto prev = std::move(root);
        diff->assign_sibling(prev.get());
        root = next = diff;
    } else {
        root = next = diff;
    }
    if (left == 0) {
        auto prev = std::move(root);
        auto diff = new model::diff::load::interrupt_t();
        diff->assign_sibling(prev.get());
        root = next = diff;
        left = batch_limit;
        result = true;
    }
    return result;
}

bool diff_assember_t::has_diffs() const noexcept { return (bool)root; }

bool diff_assember_t::is_overused() const noexcept { return total >= batch_limit; }

cluster_diff_ptr_t diff_assember_t::consume() noexcept {
    auto result = std::move(root);
    next = nullptr;
    left = batch_limit;
    return result;
}
