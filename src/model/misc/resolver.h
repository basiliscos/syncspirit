// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/file_info.h"
#include "syncspirit-export.h"

namespace syncspirit::model {

enum class advance_action_t {
    ignore = 0,
    remote_copy,
    local_update,
    resolve_remote_win,
};

advance_action_t SYNCSPIRIT_API resolve(const file_info_t &remote) noexcept;

} // namespace syncspirit::model
