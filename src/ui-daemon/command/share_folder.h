// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "../command.h"
#include "../../model/folder.h"

namespace syncspirit::daemon::command {

struct share_folder_t final : command_t {
    bool execute(governor_actor_t &) noexcept override;
    static outcome::result<command_ptr_t> construct(std::string_view in) noexcept;

    inline share_folder_t() noexcept = default;
    template <typename Folder, typename Peer>
    inline share_folder_t(Folder &&f, Peer &&p) noexcept
        : folder{std::forward<Folder>(f)}, peer{std::forward<Peer>(p)} {}

  private:
    std::string folder;
    std::string peer;
};

} // namespace syncspirit::daemon::command
