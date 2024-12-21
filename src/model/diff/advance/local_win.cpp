// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "local_win.h"
#include "model/cluster.h"
#include "../cluster_visitor.h"

using namespace syncspirit::model::diff::advance;

local_win_t::local_win_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file_,
                         std::string_view folder_id_, std::string_view peer_id_) noexcept
    : advance_t(std::move(proto_file_), folder_id_, peer_id_, advance_action_t::resolve_local_win) {

    auto &device = *cluster.get_devices().by_sha256(peer_id);
    auto folder = cluster.get_folders().by_id(folder_id);
    auto &remote_files = folder->get_folder_infos().by_device(device)->get_file_infos();
    auto remote_file = remote_files.by_name(proto_source.name());
    assert(remote_file);
    initialize(cluster, sequencer, remote_file->make_conflicting_name());
}

auto local_win_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting local_win_t, folder = {}, file = {}", folder_id, proto_local.name());
    return visitor(*this, custom);
}
