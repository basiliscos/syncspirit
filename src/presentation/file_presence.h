// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "presence.h"
#include "syncspirit-export.h"

namespace syncspirit::presentation {

struct SYNCSPIRIT_API file_presence_t : presence_t {
    using presence_t::presence_t;
};

} // namespace syncspirit::presentation
