// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "child_info.h"

namespace syncspirit::net::local_keeper {

struct unexamined_t : child_info_t {
    inline unexamined_t(child_info_t info) : child_info_t(std::move(info)) {}
};

} // namespace syncspirit::net::local_keeper
