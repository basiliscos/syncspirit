// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "model/diff/cluster_diff.h"
#include <cstdint>

namespace syncspirit::net::local_keeper {

struct stack_context_t {
    stack_context_t(std::int64_t diffs_left);
    void push(model::diff::cluster_diff_t *d) noexcept;

    std::int64_t diffs_left;
    model::diff::cluster_diff_ptr_t diff;
    model::diff::cluster_diff_t *next;
};

} // namespace syncspirit::net::local_keeper
