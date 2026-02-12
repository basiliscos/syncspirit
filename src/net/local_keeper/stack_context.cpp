// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "stack_context.h"
#include "net/local_keeper.h"

using namespace syncspirit::net::local_keeper;

stack_context_t::stack_context_t(std::int64_t diffs_left_, local_keeper_t *actor_) noexcept
    : diffs_left{diffs_left_}, actor{actor_}, slave{nullptr}, next{nullptr} {}

void stack_context_t::push(model::diff::cluster_diff_t *d) noexcept {
    --diffs_left;
    if (next) {
        next = next->assign_sibling(d);
    } else {
        next = d;
        diff = d;
    }
}

stack_context_t::~stack_context_t() {
    if (diff) {
        assert(actor);
        actor->send<model::payload::model_update_t>(actor->coordinator, std::move(diff));
    }
}
