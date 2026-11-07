// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "metadata_comparator.h"

#include <utility>

using namespace syncspirit::utils;

metadata_comparator_t::metadata_comparator_t(std::int32_t timestamp_precision_s_, bool permissions_supported_) noexcept
    : timestamp_precision_s{timestamp_precision_s_}, permissions_supported{permissions_supported_} {}

auto metadata_comparator_t::compare(const utils::timestamp_t &lhs, const utils::timestamp_t &rhs) const noexcept
    -> ordening_t {
    auto diff = lhs.seconds - rhs.seconds;
    if (diff < 0) {
        return -diff > timestamp_precision_s ? ordening_t::less : ordening_t::equivalent;
    }
    if (diff > 0) {
        return diff > timestamp_precision_s ? ordening_t::greater : ordening_t::equivalent;
    }
    return ordening_t::equivalent;
}

bool metadata_comparator_t::equal(const utils::timestamp_t &lhs, const utils::timestamp_t &rhs) const noexcept {
    return compare(lhs, rhs) == ordening_t::equivalent;
}

bool metadata_comparator_t::equal(const utils::timestamp_t &lhs, std::int64_t rhs) const noexcept {
    return equal(lhs, {rhs, 0});
}

metadata_comparator_t &metadata_comparator_t::operator=(metadata_comparator_t &&other) noexcept {
    std::swap(timestamp_precision_s, other.timestamp_precision_s);
    std::swap(permissions_supported, other.permissions_supported);
    return *this;
}
