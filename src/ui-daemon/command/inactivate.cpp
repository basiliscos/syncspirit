// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "inactivate.h"
#include "../governor_actor.h"
#include <charconv>

namespace syncspirit::daemon::command {

outcome::result<command_ptr_t> inactivate_t::construct(std::string_view in) noexcept {
    std::uint32_t seconds;
    auto pair = std::from_chars(in.data(), in.data() + in.size(), seconds);
    if (pair.ec != std::errc()) {
        return std::make_error_code(pair.ec);
    }
    return std::make_unique<command::inactivate_t>(seconds);
}

bool inactivate_t::execute(governor_actor_t &actor) noexcept {
    if (seconds) {
        actor.inactivity_seconds = seconds;
        actor.track_inactivity();
    }
    return true;
}

} // namespace syncspirit::daemon::command
