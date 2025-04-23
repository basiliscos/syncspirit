// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <cstdint>

namespace syncspirit::presentation {

struct entity_stats_t {
    std::int64_t entities = 0;
    std::int64_t size = 0;

    bool operator==(const entity_stats_t &) const noexcept = default;

    inline entity_stats_t &operator+=(const entity_stats_t &o) noexcept {
        entities += o.entities;
        size += o.size;
        return *this;
    }

    inline entity_stats_t &operator-=(const entity_stats_t &o) noexcept {
        entities -= o.entities;
        size -= o.size;
        return *this;
    }

    inline entity_stats_t operator-(const entity_stats_t &o) const noexcept {
        return {
            entities - o.entities,
            size - o.size,
        };
    }
    inline entity_stats_t operator-() const noexcept { return {-entities, -size}; }
};

struct presence_stats_t : entity_stats_t {
    std::int64_t cluster_entries = 0;

    bool operator==(const presence_stats_t &) const noexcept = default;

    inline presence_stats_t &operator+=(const presence_stats_t &o) noexcept {
        static_cast<entity_stats_t &>(*this) += o;
        cluster_entries += o.cluster_entries;
        return *this;
    }

    inline presence_stats_t &operator-=(const presence_stats_t &o) noexcept {
        static_cast<entity_stats_t &>(*this) += o;
        cluster_entries -= o.cluster_entries;
        return *this;
    }

    inline presence_stats_t operator-(const presence_stats_t &o) const noexcept {
        auto &self = static_cast<const entity_stats_t &>(*this);
        return {
            {self - o},
            cluster_entries - o.cluster_entries,
        };
    }
    inline presence_stats_t operator-() const noexcept {
        auto &self = static_cast<const entity_stats_t &>(*this);
        return {-self, -cluster_entries};
    }
};

}; // namespace syncspirit::presentation
