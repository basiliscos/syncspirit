// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <string_view>

namespace syncspirit::proto {

struct luhn32 {
    static char calculate(std::string_view in) noexcept;
    static bool validate(std::string_view in) noexcept;
};

} // namespace syncspirit::proto
