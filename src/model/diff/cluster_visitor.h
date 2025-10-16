// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "diffs_fwd.h"
#include <boost/outcome.hpp>

namespace syncspirit::model::diff {

namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API cluster_visitor_t {
    virtual outcome::result<void> operator()(const advance::advance_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const advance::local_update_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const advance::remote_copy_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const advance::remote_win_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const contact::connect_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::dial_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::ignored_connected_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::peer_state_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::relay_connect_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::unknown_connected_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::update_contact_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const load::commit_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::devices_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::ignored_devices_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::pending_devices_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::load_cluster_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::remove_corrupted_files_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const local::blocks_availability_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::custom_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::file_availability_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::io_failure_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::scan_finish_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::scan_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::scan_start_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::synchronization_start_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const local::synchronization_finish_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const peer::cluster_update_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const peer::rx_tx_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const peer::update_folder_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const modify::add_blocks_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::add_ignored_device_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::add_pending_device_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::add_pending_folders_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::add_remote_folder_infos_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::block_ack_t &, void *custom) noexcept;
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
    virtual outcome::result<void> operator()(const modify::suspend_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::unshare_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::update_peer_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::upsert_folder_info_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::upsert_folder_t &, void *custom) noexcept;
};

} // namespace syncspirit::model::diff
