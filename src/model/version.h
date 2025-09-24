// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "proto/proto-fwd.hpp"
#include "utils/vector.hpp"

namespace syncspirit::model {

struct device_t;

struct SYNCSPIRIT_API version_t final {
    using counters_t = utils::vector_t<proto::Counter>;
    version_t() noexcept;
    version_t(const proto::Vector &) noexcept;
    version_t(const device_t &) noexcept;

    proto::Vector as_proto() const noexcept;
    void to_proto(proto::Vector &) const noexcept;

    proto::Counter &get_best() noexcept;
    const proto::Counter &get_best() const noexcept;
    void update(const device_t &) noexcept;
    const proto::Counter &get_counter(size_t) noexcept;
    bool contains(const version_t &other) const noexcept;
    bool identical_to(const version_t &) const noexcept;
    size_t counters_size() const;
    const counters_t &get_counters() noexcept;

  private:
    counters_t counters;
    uint32_t best_index;
};

} // namespace syncspirit::model
