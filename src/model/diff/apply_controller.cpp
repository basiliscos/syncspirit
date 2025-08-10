// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "apply_controller.h"
#include "load/blocks.h"
#include "load/file_infos.h"
#include "load/interrupt.h"
#include "load/load_cluster.h"

namespace syncspirit::model::diff {

auto apply_controller_t::apply(const cluster_diff_t &diff, cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(cluster, *this, custom);
}

auto apply_controller_t::apply(const load::blocks_t &diff, cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(cluster, *this, custom);
}

auto apply_controller_t::apply(const load::file_infos_t &diff, cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(cluster, *this, custom);
}

auto apply_controller_t::apply(const load::interrupt_t &diff, cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(cluster, *this, custom);
}

auto apply_controller_t::apply(const load::load_cluster_t &diff, cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(cluster, *this, custom);
}

} // namespace syncspirit::model::diff
