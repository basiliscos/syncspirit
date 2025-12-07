// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "child_info.h"

namespace syncspirit::net::local_keeper {

struct incomplete_t : child_info_t {
    using child_info_t::child_info_t;
};

} // namespace syncspirit::net::local_keeper
