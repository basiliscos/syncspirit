// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "fs_context.h"
#include <rotor/supervisor.h>

using namespace syncspirit::fs;
using namespace rotor;

namespace {
namespace to {
struct state {};
struct queue {};
struct poll_duration {};
struct inbound_queue {};
} // namespace to
} // namespace

template <> auto &supervisor_t::access<to::state>() noexcept { return state; }
template <> auto &supervisor_t::access<to::queue>() noexcept { return queue; }
template <> auto &supervisor_t::access<to::poll_duration>() noexcept { return poll_duration; }
template <> auto &supervisor_t::access<to::inbound_queue>() noexcept { return inbound_queue; }

void fs_context_t::run() noexcept {
    using std::chrono::duration_cast;
    using time_units_t = std::chrono::microseconds;

    auto &root_sup = *get_supervisor();
    auto condition = [&]() -> bool { return root_sup.access<to::state>() != state_t::SHUT_DOWN; };
    auto &queue = root_sup.access<to::queue>();
    auto &inbound = root_sup.access<to::inbound_queue>();
    auto &poll_duration = root_sup.access<to::poll_duration>();

    auto try_pop_inbound = [&]() -> void {
        message_base_t *ptr;
        while (inbound.pop(ptr)) {
            queue.emplace_back(ptr, false);
        }
    };

    auto total_us = poll_duration.total_microseconds();
    auto delta = time_units_t{total_us};
    while (condition()) {
        root_sup.do_process();
        if (condition()) {
            try_pop_inbound();
            if (total_us && queue.empty()) {
                auto dealine = clock_t::now() + delta;
                if (!timer_nodes.empty()) {
                    dealine = std::min(dealine, timer_nodes.front().deadline);
                }
                // fast stage, indirect spin-lock, cpu consuming
                while (queue.empty() && (clock_t::now() < dealine)) {
                    try_pop_inbound();
                }
            }
            if (queue.empty()) {
                wait_next_event();
            }
            update_time();
        }
    }
    root_sup.do_process();
}
