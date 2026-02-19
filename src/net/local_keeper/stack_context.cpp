// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "stack_context.h"

using namespace syncspirit::net::local_keeper;

stack_context_t::stack_context_t(model::cluster_t &cluster_, model::sequencer_t &sequencer_, std::int32_t hashes_pool_,
                                 std::int32_t hashes_pool_max_, std::int32_t diffs_left_,
                                 syncspirit_watcher_impl_t watcher_impl_) noexcept
    : cluster{cluster_}, sequencer{sequencer_}, hashes_pool{hashes_pool_}, hashes_pool_max{hashes_pool_max_},
      slave{nullptr}, next{nullptr}, diffs_left{diffs_left_}, watcher_impl{watcher_impl_} {}

void stack_context_t::push(model::diff::cluster_diff_t *d) noexcept {
    --diffs_left;
    if (next) {
        next = next->assign_sibling(d);
    } else {
        next = d;
        diff = d;
    }
}
