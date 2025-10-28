// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

namespace syncspirit::model::diff {

struct cluster_diff_t;

namespace advance {
struct advance_t;
struct local_update_t;
struct remote_copy_t;
struct remote_win_t;
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
struct blocks_t;
struct commit_t;
struct devices_t;
struct file_infos_t;
struct load_cluster_t;
struct ignored_devices_t;
struct interrupt_t;
struct remove_corrupted_files_t;
struct pending_devices_t;
} // namespace load

namespace local {
struct blocks_availability_t;
struct file_availability_t;
struct io_failure_t;
struct scan_finish_t;
struct scan_request_t;
struct scan_start_t;
struct synchronization_start_t;
struct synchronization_finish_t;
} // namespace local

namespace peer {
struct cluster_update_t;
struct rx_tx_t;
struct update_folder_t;
} // namespace peer

namespace modify {
struct add_blocks_t;
struct add_ignored_device_t;
struct add_pending_device_t;
struct add_pending_folders_t;
struct add_remote_folder_infos_t;
struct block_ack_t;
struct block_transaction_t;
struct generic_remove_t;
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
struct suspend_folder_t;
struct unshare_folder_t;
struct update_peer_t;
struct upsert_folder_info_t;
struct upsert_folder_t;
} // namespace modify

} // namespace syncspirit::model::diff
