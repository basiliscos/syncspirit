// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <memory>
#include <list>
#include <boost/outcome.hpp>
#include "../utils/log.h"

namespace syncspirit::daemon {

namespace outcome = boost::outcome_v2;

struct governor_actor_t;

struct command_t;
using command_ptr_t = std::unique_ptr<command_t>;

struct command_t {
    virtual ~command_t() = default;
    virtual bool execute(governor_actor_t &) noexcept = 0;

    static outcome::result<command_ptr_t> parse(std::string_view) noexcept;
    utils::logger_t log;
};

using Commands = std::list<command_ptr_t>;

} // namespace syncspirit::daemon
