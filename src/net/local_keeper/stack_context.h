// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "model/diff/cluster_diff.h"
#include "model/misc/sequencer.h"
#include "syncspirit-config.h"
#include <cstdint>
#include <rotor/address.hpp>

namespace syncspirit::net {

namespace local_keeper {

struct folder_slave_t;

struct stack_context_t {
    stack_context_t(model::cluster_t &cluster, model::sequencer_t &sequencer, std::int32_t hashes_pool,
                    std::int32_t hashes_pool_max, std::int32_t diffs_left,
                    syncspirit_watcher_impl_t watcher_impl) noexcept;
    void push(model::diff::cluster_diff_t *d) noexcept;

    virtual bool has_in_progress_io() const noexcept = 0;
    virtual rotor::address_ptr_t get_back_address() const noexcept = 0;

    model::cluster_t &cluster;
    model::sequencer_t &sequencer;

    std::int32_t hashes_pool;
    const std::int32_t hashes_pool_max;
    std::int32_t diffs_left;
    syncspirit_watcher_impl_t watcher_impl;

    folder_slave_t *slave;
    model::diff::cluster_diff_ptr_t diff;
    model::diff::cluster_diff_t *next;
};

} // namespace local_keeper
} // namespace syncspirit::net
