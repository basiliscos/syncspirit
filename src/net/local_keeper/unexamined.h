// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "child_info.h"

namespace syncspirit::net::local_keeper {

struct unexamined_t : child_info_t {
    inline unexamined_t(child_info_t info, bool recurse_, bool recurse_children_)
        : child_info_t(std::move(info)), recurse{recurse_}, recurse_children{recurse_children_} {}
    bool recurse;
    bool recurse_children;
};

} // namespace syncspirit::net::local_keeper
