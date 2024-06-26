// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "../command.h"
#include "model/device_id.h"

namespace syncspirit::daemon::command {

struct inactivate_t final : command_t {
    static outcome::result<command_ptr_t> construct(std::string_view in) noexcept;
    bool execute(governor_actor_t &) noexcept override;

    inline inactivate_t() noexcept = default;
    inline inactivate_t(std::uint32_t seconds_) noexcept : seconds{seconds_} {}

  private:
    std::uint32_t seconds;
};

} // namespace syncspirit::daemon::command
