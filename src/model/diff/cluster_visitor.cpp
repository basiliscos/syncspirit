// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "cluster_visitor.h"
#include "cluster_diff.h"

using namespace syncspirit::model::diff;

auto cluster_visitor_t::operator()(const load::devices_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const load::ignored_devices_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const load::unknown_devices_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const load::load_cluster_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const peer::cluster_update_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const peer::update_folder_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::create_folder_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::clone_file_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::add_remote_folder_infos_t &, void *) noexcept
    -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::add_ignored_device_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::add_unknown_device_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::add_unknown_folders_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::share_folder_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::unshare_folder_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::update_folder_info_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::update_peer_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::remove_peer_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::remove_blocks_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::remove_files_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}
auto cluster_visitor_t::operator()(const modify::remove_folder_infos_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::remove_ignored_device_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::remove_unknown_device_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::remove_unknown_folders_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::file_availability_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::finish_file_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::finish_file_ack_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::local_update_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::lock_file_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::mark_reachable_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::next(const model::diff::cluster_diff_t &diff, void *custom,
                             const outcome::result<void> &prev) noexcept -> outcome::result<void> {
    if (!prev) {
        return prev;
    }
    if (diff.next) {
        return diff.next->visit(*this, custom);
    }
    return outcome::success();
}
