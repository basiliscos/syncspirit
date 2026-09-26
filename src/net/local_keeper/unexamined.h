// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "child_info.h"

namespace syncspirit::net::local_keeper {

struct unexamined_t : child_info_t {
    inline unexamined_t(child_info_t info, bool recurse_, bool recurse_children_, bool requires_refinement_)
        : child_info_t(std::move(info)), recurse{recurse_ ? 1u : 0}, recurse_children{recurse_children_ ? 1u : 0},
          requires_refinement{requires_refinement_ ? 1u : 0} {}
    unsigned recurse : 1;
    unsigned recurse_children : 1;
    unsigned requires_refinement : 1;
};

} // namespace syncspirit::net::local_keeper
