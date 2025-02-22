// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "time.h"

namespace syncspirit::utils {

std::int64_t as_seconds(const pt::ptime &t) noexcept {
    pt::ptime epoch(boost::gregorian::date(1970, 1, 1));
    auto time_diff = t - epoch;
    auto value = time_diff.ticks() / time_diff.ticks_per_second();
    return value;
}

} // namespace syncspirit::utils
