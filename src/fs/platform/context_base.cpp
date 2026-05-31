// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "context_base.h"
#include <cassert>

using namespace syncspirit::fs::platform;

context_base_t::context_base_t(const pt::time_duration &poll_timeout_) noexcept : poll_timeout{poll_timeout_} {
    log = utils::get_logger("fs");
    poll_timeout_ms = poll_timeout.total_milliseconds();
    assert(poll_timeout_ms > 0);
}

std::uint32_t context_base_t::determine_wait_ms() noexcept {
    using namespace std::chrono;
    auto timeout = poll_timeout_ms;
    // auto timeout = int{1'000};
    if (!timer_nodes.empty()) {
        auto &next = timer_nodes.front().deadline;
        auto delta = next - clock_t::now();
        auto milli_dur = duration_cast<milliseconds>(delta);
        auto milli = milli_dur.count() + 1; // to round up
        if (milli < poll_timeout_ms) {
            timeout = std::max(0, static_cast<int>(milli));
        }
    }
    return timeout;
}
