// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "syncspirit-export.h"

namespace syncspirit::model::diff {

struct cluster_diff_t;

namespace advance {
struct advance_t;
struct local_update_t;
struct remote_copy_t;
} // namespace advance

namespace contact {
struct connect_request_t;
struct dial_request_t;
struct ignored_connected_t;
struct peer_state_t;
struct relay_connect_request_t;
struct unknown_connected_t;
struct update_contact_t;
} // namespace contact

namespace load {
struct devices_t;
struct ignored_devices_t;
struct pending_devices_t;
struct load_cluster_t;
} // namespace load

namespace local {
struct blocks_availability_t;
struct custom_t;
struct file_availability_t;
struct scan_finish_t;
struct scan_request_t;
struct scan_start_t;
struct synchronization_start_t;
struct synchronization_finish_t;
} // namespace local

namespace peer {
struct cluster_update_t;
struct update_folder_t;
} // namespace peer

namespace modify {
struct add_blocks_t;
struct add_ignored_device_t;
struct add_pending_device_t;
struct add_pending_folders_t;
struct add_remote_folder_infos_t;
struct append_block_t;
struct block_ack_t;
struct block_rej_t;
struct block_transaction_t;
struct clone_block_t;
struct finish_file_t;
struct generic_remove_t;
struct lock_file_t;
struct mark_reachable_t;
struct remove_blocks_t;
struct remove_files_t;
struct remove_folder_infos_t;
struct remove_folder_t;
struct remove_ignored_device_t;
struct remove_peer_t;
struct remove_pending_device_t;
struct remove_pending_folders_t;
struct reset_folder_infos_t;
struct share_folder_t;
struct unshare_folder_t;
struct update_peer_t;
struct upsert_folder_info_t;
struct upsert_folder_t;
} // namespace modify

template <> struct SYNCSPIRIT_API generic_visitor_t<tag::cluster, cluster_diff_t> {
    virtual outcome::result<void> operator()(const advance::advance_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const advance::local_update_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const advance::remote_copy_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const contact::connect_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::dial_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::ignored_connected_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::peer_state_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::relay_connect_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::unknown_connected_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::update_contact_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const load::devices_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::ignored_devices_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::pending_devices_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::load_cluster_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::blocks_availability_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const local::custom_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::file_availability_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::scan_finish_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::scan_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::scan_start_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::synchronization_start_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::synchronization_finish_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const peer::cluster_update_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const peer::update_folder_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const modify::add_blocks_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::add_ignored_device_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::add_pending_device_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::add_pending_folders_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::add_remote_folder_infos_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::append_block_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::block_ack_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::block_rej_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::clone_block_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::finish_file_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::lock_file_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::mark_reachable_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_blocks_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_files_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_folder_infos_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_ignored_device_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_peer_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_pending_device_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::remove_pending_folders_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::reset_folder_infos_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::share_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::unshare_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::update_peer_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::upsert_folder_info_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::upsert_folder_t &, void *custom) noexcept;
};

using cluster_visitor_t = generic_visitor_t<tag::cluster, cluster_diff_t>;

} // namespace syncspirit::model::diff
