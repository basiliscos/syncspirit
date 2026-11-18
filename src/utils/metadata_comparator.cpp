// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "metadata_comparator.h"

#include <utility>

using namespace syncspirit::utils;

metadata_comparator_t::metadata_comparator_t(std::int32_t timestamp_precision_s_, bool permissions_supported_,
                                             bool symlinks_supported_) noexcept
    : timestamp_precision_s{timestamp_precision_s_}, permissions_supported{permissions_supported_},
      symlinks_supported{symlinks_supported_} {}

metadata_comparator_t &metadata_comparator_t::operator=(metadata_comparator_t &&other) noexcept {
    std::swap(timestamp_precision_s, other.timestamp_precision_s);
    std::swap(permissions_supported, other.permissions_supported);
    std::swap(symlinks_supported, other.symlinks_supported);
    return *this;
}

metadata_comparator_t metadata_comparator_t::clone() const noexcept {
    return {timestamp_precision_s, permissions_supported, symlinks_supported};
}

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

bool metadata_comparator_t::equal(const utils::permissions_t &lhs, const utils::permissions_t &rhs) const noexcept {
    if (permissions_supported) {
        if (lhs.absent || rhs.absent) {
            return true;
        }
        return lhs.value == rhs.value;
    }
    return true;
}

bool metadata_comparator_t::equal(const utils::permissions_t &lhs, std::uint32_t rhs) const noexcept {
    if (permissions_supported) {
        if (lhs.absent) {
            return true;
        }
        return lhs.value == rhs;
    }
    return true;
}
