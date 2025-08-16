// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "apply_controller.h"
#include "advance/advance.h"
#include "load/blocks.h"
#include "load/file_infos.h"
#include "load/interrupt.h"
#include "load/commit.h"
#include "load/load_cluster.h"
#include "modify/upsert_folder.h"
#include "modify/upsert_folder_info.h"
#include "peer/update_folder.h"

namespace syncspirit::model::diff {
using peer::update_folder_t;

auto apply_controller_t::apply(const cluster_diff_t &diff, cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(cluster, *this, custom);
}

auto apply_controller_t::apply(const load::blocks_t &diff, cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(cluster, *this, custom);
}

auto apply_controller_t::apply(const advance::advance_t &diff, cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(cluster, *this, custom);
}

auto apply_controller_t::apply(const load::commit_t &diff, cluster_t &cluster, void *custom) noexcept
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

auto apply_controller_t::apply(const modify::upsert_folder_t &diff, cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(cluster, *this, custom);
}

auto apply_controller_t::apply(const modify::upsert_folder_info_t &diff, cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(cluster, *this, custom);
}

auto apply_controller_t::apply(const update_folder_t &diff, cluster_t &cluster, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(cluster, *this, custom);
}

} // namespace syncspirit::model::diff
