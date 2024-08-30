// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "cluster_diff.h"
#include "cluster_visitor.h"
#include "load/devices.h"
#include "load/ignored_devices.h"
#include "load/load_cluster.h"
#include "load/pending_devices.h"
#include "local/file_availability.h"
#include "local/scan_finish.h"
#include "local/scan_request.h"
#include "local/scan_start.h"
#include "local/update.h"
#include "modify/add_blocks.h"
#include "modify/add_ignored_device.h"
#include "modify/add_remote_folder_infos.h"
#include "modify/add_pending_device.h"
#include "modify/add_pending_folders.h"
#include "modify/clone_file.h"
#include "modify/finish_file.h"
#include "modify/finish_file_ack.h"
#include "modify/lock_file.h"
#include "modify/mark_reachable.h"
#include "modify/remove_blocks.h"
#include "modify/remove_files.h"
#include "modify/remove_folder.h"
#include "modify/remove_folder_infos.h"
#include "modify/remove_ignored_device.h"
#include "modify/remove_peer.h"
#include "modify/remove_pending_device.h"
#include "modify/remove_pending_folders.h"
#include "modify/share_folder.h"
#include "modify/unshare_folder.h"
#include "modify/update_peer.h"
#include "modify/upsert_folder.h"
#include "modify/upsert_folder_info.h"
#include "peer/cluster_update.h"
#include "peer/update_folder.h"

using namespace syncspirit::model::diff;

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

auto cluster_visitor_t::operator()(const local::file_availability_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const local::update_t &diff, void *custom) noexcept -> outcome::result<void> {
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

auto cluster_visitor_t::operator()(const peer::cluster_update_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const peer::update_folder_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::clone_file_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::add_blocks_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::add_remote_folder_infos_t &diff, void *custom) noexcept
    -> outcome::result<void> {
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

auto cluster_visitor_t::operator()(const modify::finish_file_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::finish_file_ack_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::lock_file_t &diff, void *custom) noexcept -> outcome::result<void> {
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
