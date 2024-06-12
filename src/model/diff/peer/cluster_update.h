// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <memory>
#include <set>
#include "proto/bep_support.h"
#include "model/diff/cluster_diff.h"
#include "model/device.h"
#include "model/folder_info.h"

namespace syncspirit::model::diff::peer {

struct SYNCSPIRIT_API cluster_update_t final : cluster_diff_t {
    using message_t = proto::ClusterConfig;
    struct update_info_t {
        std::string folder_id;
        proto::Device device;
    };
    using modified_folders_t = std::vector<update_info_t>;

    static outcome::result<cluster_diff_ptr_t> create(const cluster_t &cluster, const model::device_t &source,
                                                      const message_t &message) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    std::string peer_id;
    modified_folders_t remote_folders;

  private:
    cluster_update_t(const model::device_t &source, modified_folders_t remote_folders,
                     cluster_diff_ptr_t inner_diff) noexcept;
};

} // namespace syncspirit::model::diff::peer
