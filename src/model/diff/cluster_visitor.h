// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "syncspirit-export.h"

namespace syncspirit::model::diff {

struct cluster_diff_t;

namespace load {
struct devices_t;
struct ignored_devices_t;
struct pending_devices_t;
struct load_cluster_t;
} // namespace load

namespace local {
struct file_availability_t;
struct scan_finish_t;
struct scan_request_t;
struct scan_start_t;
struct update_t;
} // namespace local

namespace peer {
struct cluster_update_t;
struct update_folder_t;
} // namespace peer

namespace modify {
struct add_ignored_device_t;
struct add_remote_folder_infos_t;
struct add_pending_device_t;
struct add_pending_folders_t;
struct clone_file_t;
struct upsert_folder_t;
struct finish_file_t;
struct finish_file_ack_t;
struct lock_file_t;
struct mark_reachable_t;
struct unshare_folder_t;
struct share_folder_t;
struct update_peer_t;
struct generic_remove_t;
struct remove_peer_t;
struct remove_blocks_t;
struct remove_files_t;
struct remove_folder_t;
struct remove_folder_infos_t;
struct remove_ignored_device_t;
struct remove_pending_device_t;
struct remove_pending_folders_t;
struct upsert_folder_info_t;
} // namespace modify

template <> struct SYNCSPIRIT_API generic_visitor_t<tag::cluster, cluster_diff_t> {
    virtual outcome::result<void> operator()(const load::devices_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::ignored_devices_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::pending_devices_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::load_cluster_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const local::file_availability_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::update_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::scan_finish_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::scan_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::scan_start_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const peer::cluster_update_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const peer::update_folder_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const modify::add_remote_folder_infos_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::add_ignored_device_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::add_pending_device_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::add_pending_folders_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::clone_file_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::upsert_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::finish_file_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::finish_file_ack_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::lock_file_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::mark_reachable_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::share_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::unshare_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::update_peer_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_peer_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_blocks_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_files_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_folder_infos_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_ignored_device_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_pending_device_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_pending_folders_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::upsert_folder_info_t &, void *custom) noexcept;
};

using cluster_visitor_t = generic_visitor_t<tag::cluster, cluster_diff_t>;

} // namespace syncspirit::model::diff
