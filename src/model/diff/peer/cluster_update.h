// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "model/misc/sequencer.h"
#include "model/diff/cluster_diff.h"

#include <filesystem>

namespace syncspirit::model::diff::peer {

namespace sys = boost::system;
namespace bfs = std::filesystem;

struct SYNCSPIRIT_API cluster_update_t final : cluster_diff_t {
    using message_t = proto::ClusterConfig;
    using parent_t = cluster_diff_t;

    static outcome::result<cluster_diff_ptr_t> create(const bfs::path &default_path, const cluster_t &cluster,
                                                      sequencer_t &sequencer, const model::device_t &source,
                                                      const message_t &message) noexcept;

    outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept override;
    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    cluster_update_t(const bfs::path &default_path, const cluster_t &cluster, sequencer_t &sequencer,
                     const model::device_t &source, const message_t &message) noexcept;

    sys::error_code ec;
    utils::bytes_t peer_id;
};

} // namespace syncspirit::model::diff::peer
