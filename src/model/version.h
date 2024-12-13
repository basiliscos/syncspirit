// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "misc/arc.hpp"
#include "syncspirit-export.h"
#include "bep.pb.h"
#include <vector>

namespace syncspirit::model {

struct device_t;

struct SYNCSPIRIT_API version_t final : arc_base_t<version_t> {
    version_t(const proto::Vector &) noexcept;
    version_t(const device_t &) noexcept;

    proto::Vector as_proto() const noexcept;
    void to_proto(proto::Vector &) const noexcept;

    proto::Counter &get_best() noexcept;
    const proto::Counter &get_best() const noexcept;
    void update(const device_t &) noexcept;
    const proto::Counter &get_counter(size_t) noexcept;
    bool contains(const version_t &other) noexcept;
    bool identical_to(const version_t &) noexcept;
    size_t counters_count() const;

  private:
    using counters_t = std::vector<proto::Counter>;
    counters_t counters;
    size_t best_index;
};

using version_ptr_t = intrusive_ptr_t<version_t>;

} // namespace syncspirit::model
