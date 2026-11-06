// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "utils/timestamp.h"
#include <compare>

namespace syncspirit::model {

struct SYNCSPIRIT_API metadata_comparator_t {
    using ordening_t = std::strong_ordering;

    metadata_comparator_t(std::int32_t timestamp_precision_s, bool permissions_supported) noexcept;
    metadata_comparator_t(metadata_comparator_t &&) = default;
    metadata_comparator_t(const metadata_comparator_t &) = delete;

    metadata_comparator_t &operator=(metadata_comparator_t &&other) noexcept;

    bool equal(const utils::timestamp_t &lhs, const utils::timestamp_t &rhs) const noexcept;
    bool equal(const utils::timestamp_t &lhs, std::int64_t) const noexcept;
    ordening_t compare(const utils::timestamp_t &lhs, const utils::timestamp_t &rhs) const noexcept;

    std::int32_t timestamp_precision_s = 0;
    bool permissions_supported = true;
};

}; // namespace syncspirit::model