// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <memory>
#include <set>
#include "proto/bep_support.h"
#include "../cluster_diff.h"
#include "model/device.h"
#include "model/folder_info.h"
#include "utils/string_comparator.hpp"

namespace syncspirit::model::diff::peer {

struct SYNCSPIRIT_API cluster_update_t final : cluster_diff_t {
    using message_t = proto::ClusterConfig;
    using unknown_folders_t = std::vector<proto::Folder>;
    struct update_info_t {
        std::string folder_id;
        proto::Device device;
    };
    using modified_folders_t = std::vector<update_info_t>;
    using keys_t = std::set<std::string, utils::string_comparator_t>;

    static outcome::result<cluster_diff_ptr_t> create(const cluster_t &cluster, const model::device_t &source,
                                                      const message_t &message) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    model::device_t source_peer;
    unknown_folders_t new_unknown_folders;
    modified_folders_t remote_folders;
    keys_t removed_files;

  private:
    cluster_update_t(const model::device_t &source, unknown_folders_t unknown_folders,
                     modified_folders_t remote_folders, cluster_diff_ptr_t inner_diff) noexcept;
};

} // namespace syncspirit::model::diff::peer
