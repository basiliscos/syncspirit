// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#pragma once

#include <string_view>

namespace syncspirit::fltk::symbols {

extern const std::string_view online_1;
extern const std::string_view online_2;
extern const std::string_view offline;
extern const std::string_view connecting;
extern const std::string_view discovering;
extern const std::string_view scanning;
extern const std::string_view synchronizing;
extern const std::string_view deleted;
extern const std::string_view missing;
extern const std::string_view colorize;

std::string_view get_description(std::string_view symbol);

} // namespace syncspirit::fltk::symbols
