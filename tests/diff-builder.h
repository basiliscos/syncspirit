// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <boost/filesystem.hpp>
#include <boost/outcome.hpp>
#include <rotor/supervisor.h>

#include "syncspirit-test-export.h"
#include "model/device.h"
#include "model/ignored_device.h"
#include "model/pending_device.h"
#include "model/file_info.h"
#include "model/diff/cluster_diff.h"
#include "model/diff/block_diff.h"
#include "model/diff/modify/block_transaction.h"
#include "model/misc/sequencer.h"

namespace syncspirit::test {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

struct diff_builder_t;

struct SYNCSPIRIT_TEST_API cluster_configurer_t {
    cluster_configurer_t(diff_builder_t &builder, std::string_view peer_sha256) noexcept;
    cluster_configurer_t &&add(std::string_view sha256, std::string_view folder_id, uint64_t index,
                               int64_t max_sequence) noexcept;
    diff_builder_t &finish() noexcept;

  private:
    proto::ClusterConfig cc;
    proto::Folder *folder;
    diff_builder_t &builder;
    std::string_view peer_sha256;
};

struct SYNCSPIRIT_TEST_API index_maker_t {
    index_maker_t(diff_builder_t &builder, std::string_view peer_sha256, std::string_view folder_id) noexcept;
    index_maker_t &&add(const proto::FileInfo &, const model::device_ptr_t &peer) noexcept;
    diff_builder_t &finish() noexcept;
    std::error_code fail() noexcept;

  private:
    proto::Index index;
    diff_builder_t &builder;
    std::string_view peer_sha256;
};

struct SYNCSPIRIT_TEST_API diff_builder_t {
    using blocks_t = std::vector<proto::BlockInfo>;
    using dispose_callback_t = model::diff::modify::block_transaction_t::dispose_callback_t;

    diff_builder_t(model::cluster_t &) noexcept;
    cluster_configurer_t configure_cluster(std::string_view sha256) noexcept;
    diff_builder_t &apply(r::supervisor_t &sup) noexcept;
    outcome::result<void> apply() noexcept;
    index_maker_t make_index(std::string_view sha256, std::string_view folder_id) noexcept;

    diff_builder_t &upsert_folder(std::string_view id, std::string_view path, std::string_view label = "",
                                  std::uint64_t index_id = 0) noexcept;
    diff_builder_t &upsert_folder(const db::Folder &data, std::uint64_t index_id = 0) noexcept;
    diff_builder_t &update_peer(const model::device_id_t &device, std::string_view name = "",
                                std::string_view cert_name = "", bool auto_accept = true) noexcept;
    diff_builder_t &share_folder(std::string_view sha256, std::string_view folder_id) noexcept;
    diff_builder_t &unshare_folder(model::folder_info_t &fi) noexcept;
    diff_builder_t &clone_file(const model::file_info_t &source) noexcept;
    diff_builder_t &finish_file(const model::file_info_t &file) noexcept;
    diff_builder_t &local_update(std::string_view folder_id, const proto::FileInfo &file_) noexcept;
    diff_builder_t &append_block(const model::file_info_t &target, size_t block_index, std::string data,
                                 dispose_callback_t) noexcept;
    diff_builder_t &clone_block(const model::file_block_t &, dispose_callback_t) noexcept;
    diff_builder_t &ack_block(const model::diff::modify::block_transaction_t &) noexcept;
    diff_builder_t &remove_folder(const model::folder_t &folder) noexcept;
    diff_builder_t &remove_peer(const model::device_t &peer) noexcept;
    diff_builder_t &update_state(const model::device_t &peer, const r::address_ptr_t &peer_addr,
                                 model::device_state_t state) noexcept;
    diff_builder_t &update_contact(const model::device_id_t &device, const utils::uri_container_t &uris) noexcept;
    diff_builder_t &add_ignored_device(const model::device_id_t &device, db::SomeDevice db_device) noexcept;
    diff_builder_t &add_unknown_device(const model::device_id_t &device, db::SomeDevice db_device) noexcept;
    diff_builder_t &remove_ignored_device(const model::ignored_device_t &device) noexcept;
    diff_builder_t &remove_unknown_device(const model::pending_device_t &device) noexcept;
    diff_builder_t &scan_start(std::string_view id, const r::pt::ptime & = {}) noexcept;
    diff_builder_t &scan_finish(std::string_view id, const r::pt::ptime & = {}) noexcept;
    diff_builder_t &scan_request(std::string_view id) noexcept;
    diff_builder_t &synchronization_start(std::string_view id) noexcept;
    diff_builder_t &synchronization_finish(std::string_view id) noexcept;
    diff_builder_t &mark_reacheable(model::file_info_ptr_t peer_file, bool value) noexcept;

    model::sequencer_t &get_sequencer() noexcept;

    diff_builder_t &assign(model::diff::cluster_diff_t *) noexcept;

  private:
    model::sequencer_ptr_t sequencer;
    model::cluster_t &cluster;
    model::diff::cluster_diff_ptr_t cluster_diff;

    friend struct cluster_configurer_t;
    friend struct index_maker_t;
};

} // namespace syncspirit::test
