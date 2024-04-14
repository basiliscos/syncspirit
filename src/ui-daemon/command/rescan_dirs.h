// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023 Ivan Baidakou

#pragma once

#include <cstdint>
#include "../command.h"
#include "model/folder.h"

namespace syncspirit::daemon::command {

struct rescan_dirs_t final : command_t {
    bool execute(governor_actor_t &) noexcept override;
    static outcome::result<command_ptr_t> construct(std::string_view in) noexcept;

    inline rescan_dirs_t() noexcept = default;
    inline rescan_dirs_t(std::uint32_t seconds_) noexcept : seconds{seconds_} {}

  private:
    std::uint32_t seconds;
};

} // namespace syncspirit::daemon::command
