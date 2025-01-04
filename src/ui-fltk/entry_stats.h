// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <cstdint>

namespace syncspirit::fltk {

struct entry_stats_t {
    inline entry_stats_t() : sequence{0}, entries{0}, scanned_entries{0}, entries_size{0} {}
    std::int64_t sequence;
    std::int64_t entries;
    std::int64_t scanned_entries;
    std::int64_t entries_size;
};

} // namespace syncspirit::fltk
