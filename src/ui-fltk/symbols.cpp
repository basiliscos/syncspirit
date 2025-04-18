// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "symbols.h"

#define UTF8_CAST(X) std::string_view(reinterpret_cast<const char *>((X)))

namespace syncspirit::fltk::symbols {

static const auto scanning_raw = u8"∴";
static const auto synchronizing_raw = u8"↓";
static const auto online_raw = u8"↔";
static const auto offline_raw = u8"▽";
static const auto connecting_raw = u8"→";
static const auto discovering_raw = u8"…";
static const auto deleted_raw = u8"☠";
static const auto missing_raw = u8"-";
static const auto colorize_raw = u8"★";

// https://www.vertex42.com/ExcelTips/unicode-symbols.html

// ♨
// ♻
// ⚙
// ✆
// °
// …
// ≈
// ∞
// ∴
// ↑
// ≈

const std::string_view scanning = UTF8_CAST(scanning_raw);
const std::string_view synchronizing = UTF8_CAST(synchronizing_raw);
const std::string_view online = UTF8_CAST(online_raw);
const std::string_view offline = UTF8_CAST(offline_raw);
const std::string_view connecting = UTF8_CAST(connecting_raw);
const std::string_view discovering = UTF8_CAST(discovering_raw);
const std::string_view deleted = UTF8_CAST(deleted_raw);
const std::string_view missing = UTF8_CAST(missing_raw);
const std::string_view colorize = UTF8_CAST(colorize_raw);

std::string_view get_description(std::string_view symbol) {
    if (symbol == scanning) {
        return "scanning";
    } else if (symbol == synchronizing) {
        return "synchronizing";
    } else if (symbol == online) {
        return "online";
    } else if (symbol == offline) {
        return "offline";
    } else if (symbol == connecting) {
        return "connecting";
    }
    return "?";
}

} // namespace syncspirit::fltk::symbols
