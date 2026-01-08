// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "stack_context.h"

using namespace syncspirit::net::local_keeper;

stack_context_t::stack_context_t(std::int64_t diffs_left_) : diffs_left{diffs_left_}, next{nullptr} {}

void stack_context_t::push(model::diff::cluster_diff_t *d) noexcept {
    --diffs_left;
    if (next) {
        next = next->assign_sibling(d);
    } else {
        next = d;
        diff = d;
    }
}
