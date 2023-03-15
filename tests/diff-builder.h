// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "syncspirit-test-export.h"
#include <boost/filesystem.hpp>
#include <rotor/supervisor.h>
#include "model/device.h"
#include "model/file_info.h"
#include "model/diff/cluster_diff.h"
#include "model/diff/block_diff.h"
#include "model/diff/aggregate.h"

namespace syncspirit::test {

namespace r = rotor;

struct diff_builder_t;

struct SYNCSPIRIT_TEST_API cluster_configurer_t {
    cluster_configurer_t(diff_builder_t &builder, std::string_view peer_sha256) noexcept;
    cluster_configurer_t &&add(std::string_view sha256, std::string_view folder_id, uint64_t index,
                               int64_t max_sequence) noexcept;
    diff_builder_t &finish() noexcept;

  private:
    proto::ClusterConfig cc;
    diff_builder_t &builder;
    std::string_view peer_sha256;
};

struct SYNCSPIRIT_TEST_API index_maker_t {
    index_maker_t(diff_builder_t &builder, std::string_view peer_sha256, std::string_view folder_id) noexcept;
    index_maker_t &&add(const proto::FileInfo &) noexcept;
    diff_builder_t &finish() noexcept;

  private:
    proto::Index index;
    diff_builder_t &builder;
    std::string_view peer_sha256;
};

struct SYNCSPIRIT_TEST_API diff_builder_t {
    using blocks_t = std::vector<proto::BlockInfo>;

    diff_builder_t(model::cluster_t &) noexcept;
    diff_builder_t &apply(r::supervisor_t &sup) noexcept;
    diff_builder_t &create_folder(std::string_view id, std::string_view path, std::string_view label = "") noexcept;
    diff_builder_t &update_peer(std::string_view sha256, std::string_view name = "", std::string_view cert_name = "",
                                bool auto_accept = true) noexcept;
    cluster_configurer_t configure_cluster(std::string_view sha256) noexcept;
    index_maker_t make_index(std::string_view sha256, std::string_view folder_id) noexcept;
    diff_builder_t &share_folder(std::string_view sha256, std::string_view folder_id) noexcept;
    diff_builder_t &clone_file(const model::file_info_t &source) noexcept;
    diff_builder_t &finish_file(const model::file_info_t &source) noexcept;
    diff_builder_t &flush_file(const model::file_info_t &source) noexcept;
    diff_builder_t &new_file(std::string_view folder_id, const proto::FileInfo &file_, const blocks_t = {}) noexcept;
    diff_builder_t &append_block(const model::file_info_t &target, size_t block_index, std::string data) noexcept;
    diff_builder_t &clone_block(const model::file_block_t &) noexcept;

  private:
    using bdiffs_t = std::vector<model::diff::block_diff_ptr_t>;
    using diffs_t = model::diff::aggregate_t::diffs_t;
    model::cluster_t &cluster;
    diffs_t diffs;
    bdiffs_t bdiffs;
    friend struct cluster_configurer_t;
    friend struct index_maker_t;
};

} // namespace syncspirit::test
