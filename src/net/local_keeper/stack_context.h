// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "model/diff/cluster_diff.h"
#include <cstdint>

namespace syncspirit::net {

struct local_keeper_t;

namespace local_keeper {

struct folder_slave_t;

struct stack_context_t {
    stack_context_t(std::int64_t diffs_left, local_keeper_t *actor) noexcept;
    ~stack_context_t();
    void push(model::diff::cluster_diff_t *d) noexcept;

    std::int64_t diffs_left;
    local_keeper_t *actor;
    folder_slave_t *slave;
    model::diff::cluster_diff_ptr_t diff;
    model::diff::cluster_diff_t *next;
};

} // namespace local_keeper
} // namespace syncspirit::net
