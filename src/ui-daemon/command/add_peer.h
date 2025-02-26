// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "../command.h"
#include "model/device_id.h"
#include "proto/proto-helpers.h"

namespace syncspirit::daemon::command {

struct add_peer_t final : command_t {
    static outcome::result<command_ptr_t> construct(std::string_view in) noexcept;
    bool execute(governor_actor_t &) noexcept override;

    inline add_peer_t() noexcept = default;
    inline add_peer_t(model::device_id_t peer_, std::string_view label_) noexcept : peer{peer_}, label(label_) {}

  private:
    model::device_id_t peer;
    std::string label;
};

} // namespace syncspirit::daemon::command
