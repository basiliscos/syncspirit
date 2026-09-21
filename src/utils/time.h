// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#pragma once

#include <boost/date_time/posix_time/posix_time.hpp>
#include <cstdint>
#include "syncspirit-export.h"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace syncspirit::utils {

namespace pt = boost::posix_time;

SYNCSPIRIT_API std::int64_t as_seconds(const pt::ptime &t) noexcept;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
static inline std::int64_t to_unix(const FILETIME &ft) {
    constexpr std::int64_t UNIX_TIME_START = 0x019DB1DED53E8000ll; // January 1, 1970 (start of Unix epoch) in "ticks"
    auto v = ((std::int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    // convert to seconds since 1601
    auto u = v - UNIX_TIME_START;
    return u / 10000000ULL;
}
#endif

} // namespace syncspirit::utils
