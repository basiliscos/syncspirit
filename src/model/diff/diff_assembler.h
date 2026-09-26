// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "cluster_diff.h"
#include <cstdint>

namespace syncspirit::model::diff {

struct SYNCSPIRIT_API diff_assember_t {
    diff_assember_t(std::uint_fast32_t batch_limit) noexcept;

    bool push_back(cluster_diff_t *next) noexcept;
    bool push_front(cluster_diff_t *next) noexcept;
    bool has_diffs() const noexcept;
    bool is_overused() const noexcept;

    cluster_diff_ptr_t consume() noexcept;

    std::uint_fast32_t batch_limit;
    std::uint_fast32_t left;
    std::uint_fast32_t total;
    cluster_diff_ptr_t root;
    cluster_diff_t *next;
};

} // namespace syncspirit::model::diff
