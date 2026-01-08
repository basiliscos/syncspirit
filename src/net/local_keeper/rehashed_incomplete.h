// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "child_ready.h"
#include "model/misc/resolver.h"

namespace syncspirit::net::local_keeper {

struct rehashed_incomplete_t : child_ready_t {
    inline rehashed_incomplete_t(child_info_t info, blocks_t blocks_, model::advance_action_t action_)
        : child_ready_t(std::move(info), std::move(blocks_)), action{action_} {}
    model::advance_action_t action;
};

} // namespace syncspirit::net::local_keeper
