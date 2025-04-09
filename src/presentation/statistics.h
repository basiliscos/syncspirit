// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <cstdint>

namespace syncspirit::presentation {

struct statistics_t {
    std::int64_t entities = 0;
    std::int64_t size = 0;

    bool operator==(const statistics_t &) const noexcept = default;
    inline statistics_t &operator+=(const statistics_t &o) noexcept {
        entities += o.entities;
        size += o.size;
        return *this;
    }

    inline statistics_t operator-(const statistics_t &o) noexcept {
        return {
            entities - o.entities,
            size - o.size,
        };
    }
};

}; // namespace syncspirit::presentation
