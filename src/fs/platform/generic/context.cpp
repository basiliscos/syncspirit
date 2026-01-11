// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "context.h"

#if !defined(SYNCSPIRIT_WATCHER_ANY)

using namespace syncspirit::fs::platform::generic;
using namespace rotor;

namespace {
namespace to {
struct inbound_queue {};
} // namespace to
} // namespace

template <> auto &supervisor_t::access<to::inbound_queue>() noexcept { return inbound_queue; }

void platform_context_t::notify() noexcept {
    std::lock_guard<std::mutex> lock(ctx->mutex);
    ctx->cv.notify_one();
}

void platform_context_t::wait_next_event() noexcept {
    using namespace std::chrono_literals;
    std::unique_lock<std::mutex> lock(mutex);
    auto &root_sup = *get_supervisor();
    auto &inbound = root_sup.access<to::inbound_queue>();
    auto predicate = [&]() { return !inbound.empty(); };
    // wait notification, do not consume CPU
    auto deadline = !timer_nodes.empty() ? timer_nodes.front().deadline : clock_t::now() + 1min;
    cv.wait_until(lock, deadline, predicate);
}

#endif
