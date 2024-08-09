// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "cluster_diff.h"
#include "cluster_visitor.h"
#include "load/devices.h"
#include "load/ignored_devices.h"
#include "load/load_cluster.h"
#include "load/unknown_devices.h"
#include "modify/add_ignored_device.h"
#include "modify/add_remote_folder_infos.h"
#include "modify/add_unknown_device.h"
#include "modify/add_unknown_folders.h"
#include "modify/clone_file.h"
#include "modify/create_folder.h"
#include "modify/file_availability.h"
#include "modify/finish_file.h"
#include "modify/finish_file_ack.h"
#include "modify/local_update.h"
#include "modify/lock_file.h"
#include "modify/mark_reachable.h"
#include "modify/remove_blocks.h"
#include "modify/remove_files.h"
#include "modify/remove_folder.h"
#include "modify/remove_folder_infos.h"
#include "modify/remove_ignored_device.h"
#include "modify/remove_peer.h"
#include "modify/remove_unknown_device.h"
#include "modify/remove_unknown_folders.h"
#include "modify/share_folder.h"
#include "modify/unshare_folder.h"
#include "modify/update_peer.h"
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

auto cluster_visitor_t::operator()(const load::unknown_devices_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const load::load_cluster_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const peer::cluster_update_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const peer::update_folder_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::create_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::clone_file_t &diff, void *custom) noexcept -> outcome::result<void> {
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

auto cluster_visitor_t::operator()(const modify::add_unknown_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::add_unknown_folders_t &diff, void *custom) noexcept
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

auto cluster_visitor_t::operator()(const modify::remove_unknown_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::remove_unknown_folders_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto cluster_visitor_t::operator()(const modify::file_availability_t &diff, void *custom) noexcept
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

auto cluster_visitor_t::operator()(const modify::local_update_t &diff, void *custom) noexcept -> outcome::result<void> {
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
