// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023 Ivan Baidakou

#include "rescan_dirs.h"
#include "../governor_actor.h"
#include "../error_code.h"
#include "utils/format.hpp"
#include <random>
#include <charconv>

namespace bfs = boost::filesystem;

namespace syncspirit::daemon::command {

outcome::result<command_ptr_t> rescan_dirs_t::construct(std::string_view in) noexcept {

    uint32_t seconds{};
    auto [ptr, ec]{std::from_chars(in.data(), in.data() + in.size(), seconds)};

    if (ec != std::errc()) {
        return make_error_code(error_code_t::incorrect_number);
    }

    return command_ptr_t(new rescan_dirs_t(seconds));
}

bool rescan_dirs_t::execute(governor_actor_t &actor) noexcept {
    using namespace model::diff;
    log = actor.log;
    auto interval = r::pt::seconds{seconds};
    actor.schedule_rescan_dirs(interval);
    return true;
}

} // namespace syncspirit::daemon::command
