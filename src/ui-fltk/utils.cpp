// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "utils.hpp"
#include <fmt/format.h>
#include <array>
#include <string_view>

namespace syncspirit::fltk {

static std::array<std::string_view, 5> suffix = {
    "B", "KB", "MB", "GB", "TB",
};

std::string get_file_size(std::int64_t value) {
    auto v = double(value);
    size_t i = 0;
    for (; i < suffix.size() && v > 1024; ++i) {
        v /= 1024.0;
    }
    if (i) {
        return fmt::format("{:.2f}{} ({}b)", v, suffix[i], value);
    }
    return fmt::format("{}b", value);
}

} // namespace syncspirit::fltk
