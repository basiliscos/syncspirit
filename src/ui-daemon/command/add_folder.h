// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "../command.h"
#include "proto/proto-helpers.h"
#include "model/folder.h"

namespace syncspirit::daemon::command {

struct add_folder_t final : command_t {
    bool execute(governor_actor_t &) noexcept override;
    static outcome::result<command_ptr_t> construct(std::string_view in) noexcept;

    inline add_folder_t() noexcept = default;
    inline add_folder_t(db::Folder &&f) noexcept : folder{std::move(f)} {}

  private:
    db::Folder folder;
};

} // namespace syncspirit::daemon::command
