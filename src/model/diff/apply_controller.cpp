// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "apply_controller.h"
#include "advance/advance.h"
#include "load/blocks.h"
#include "load/file_infos.h"
#include "load/interrupt.h"
#include "load/commit.h"
#include "load/load_cluster.h"
#include "local/io_failure.h"
#include "modify/add_ignored_device.h"
#include "modify/add_pending_device.h"
#include "modify/add_pending_folders.h"
#include "modify/update_peer.h"
#include "modify/upsert_folder.h"
#include "modify/upsert_folder_info.h"
#include "peer/update_folder.h"

namespace syncspirit::model::diff {
using peer::update_folder_t;

apply_controller_t::~apply_controller_t() {}

auto apply_controller_t::apply(const cluster_diff_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const load::blocks_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const advance::advance_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}
auto apply_controller_t::apply(const load::commit_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const load::file_infos_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const load::interrupt_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const load::load_cluster_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const local::io_failure_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const modify::add_pending_folders_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const modify::add_pending_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const modify::add_ignored_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const modify::update_peer_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const modify::upsert_folder_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const modify::upsert_folder_info_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

auto apply_controller_t::apply(const update_folder_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.apply_impl(*this, custom);
}

} // namespace syncspirit::model::diff
