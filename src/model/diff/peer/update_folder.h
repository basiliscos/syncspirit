// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include "model/cluster.h"
#include "model/misc/sequencer.h"
#include "model/misc/orphaned_blocks.h"
#include "proto/proto-fwd.hpp"

namespace syncspirit::model::diff::peer {

struct SYNCSPIRIT_API update_folder_t final : cluster_diff_t {
    using files_t = std::vector<proto::FileInfo>;
    using uuids_t = std::vector<bu::uuid>;
    using blocks_t = std::vector<proto::BlockInfo>;

    static outcome::result<cluster_diff_ptr_t> create(const cluster_t &cluster, sequencer_t &sequencer,
                                                      const model::device_t &source,
                                                      const proto::IndexBase &message) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &, void *) const noexcept override;
    outcome::result<void> apply_forward(cluster_t &, apply_controller_t &, void *) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    update_folder_t(std::string_view folder_id, utils::bytes_view_t peer_id, files_t files, uuids_t uuids,
                    blocks_t new_blocks, orphaned_blocks_t::set_t removed_blocks) noexcept;

    std::string folder_id;
    utils::bytes_t peer_id;
    files_t files;

  private:
    uuids_t uuids;
};

} // namespace syncspirit::model::diff::peer
