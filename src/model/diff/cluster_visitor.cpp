// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "cluster_diff.h"
#include "cluster_visitor.h"
#include "advance/local_update.h"
#include "advance/remote_copy.h"
#include "advance/remote_win.h"
#include "contact/connect_request.h"
#include "contact/dial_request.h"
#include "contact/ignored_connected.h"
#include "contact/peer_state.h"
#include "contact/relay_connect_request.h"
#include "contact/unknown_connected.h"
#include "contact/update_contact.h"
#include "load/commit.h"
#include "load/devices.h"
#include "load/ignored_devices.h"
#include "load/load_cluster.h"
#include "load/pending_devices.h"
#include "load/remove_corrupted_files.h"
#include "local/blocks_availability.h"
#include "local/io_failure.h"
#include "local/file_availability.h"
#include "local/scan_finish.h"
#include "local/scan_request.h"
#include "local/scan_start.h"
#include "local/synchronization_finish.h"
#include "local/synchronization_start.h"
#include "modify/add_blocks.h"
#include "modify/add_ignored_device.h"
#include "modify/add_pending_device.h"
#include "modify/add_pending_folders.h"
#include "modify/block_ack.h"
#include "modify/mark_reachable.h"
#include "modify/remove_blocks.h"
#include "modify/remove_files.h"
#include "modify/remove_folder.h"
#include "modify/remove_folder_infos.h"
#include "modify/remove_ignored_device.h"
#include "modify/remove_peer.h"
#include "modify/remove_pending_device.h"
#include "modify/remove_pending_folders.h"
#include "modify/reset_folder_infos.h"
#include "modify/share_folder.h"
#include "modify/suspend_folder.h"
#include "modify/unshare_folder.h"
#include "modify/update_peer.h"
#include "modify/upsert_folder.h"
#include "modify/upsert_folder_info.h"
#include "peer/cluster_update.h"
#include "peer/rx_tx.h"
#include "peer/update_folder.h"
#include "peer/update_remote_views.h"

using namespace syncspirit::model::diff;

auto cluster_visitor_t::operator()(const advance::advance_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const advance::local_update_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &advance_diff = static_cast<const advance::advance_t &>(diff);
    return (*this)(advance_diff, custom);
}

auto cluster_visitor_t::operator()(const advance::remote_copy_t &diff, void *custom) noexcept -> outcome::result<void> {
    auto &advance_diff = static_cast<const advance::advance_t &>(diff);
    return (*this)(advance_diff, custom);
}

auto cluster_visitor_t::operator()(const advance::remote_win_t &diff, void *custom) noexcept -> outcome::result<void> {
    auto &advance_diff = static_cast<const advance::advance_t &>(diff);
    return (*this)(advance_diff, custom);
}

auto cluster_visitor_t::operator()(const contact::connect_request_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const contact::dial_request_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const contact::ignored_connected_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const contact::relay_connect_request_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const contact::peer_state_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const contact::unknown_connected_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const contact::update_contact_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const load::commit_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const load::devices_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const load::ignored_devices_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const load::pending_devices_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const load::load_cluster_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const load::remove_corrupted_files_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const local::file_availability_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const local::io_failure_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const local::scan_finish_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const local::scan_request_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const local::scan_start_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const local::synchronization_start_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const local::synchronization_finish_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const peer::cluster_update_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const peer::rx_tx_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const peer::update_folder_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const peer::update_remote_views_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const local::blocks_availability_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::block_ack_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::add_blocks_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::add_ignored_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::add_pending_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::add_pending_folders_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::share_folder_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::suspend_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::unshare_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::update_peer_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::remove_peer_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::remove_blocks_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::remove_files_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}
auto cluster_visitor_t::operator()(const modify::remove_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::remove_folder_infos_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::remove_ignored_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::remove_pending_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::remove_pending_folders_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::reset_folder_infos_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::mark_reachable_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::upsert_folder_info_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}
