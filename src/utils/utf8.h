// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include <string_view>

namespace syncspirit::utils {

SYNCSPIRIT_API bool is_utf8_valid(std::string_view string) noexcept;

}
