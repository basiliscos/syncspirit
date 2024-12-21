// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "remote_win.h"
#include "model/cluster.h"
#include "../cluster_visitor.h"

using namespace syncspirit::model::diff::advance;

remote_win_t::remote_win_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file_,
                           std::string_view folder_id_, std::string_view peer_id_) noexcept
    : advance_t(std::move(proto_file_), folder_id_, peer_id_, advance_action_t::resolve_remote_win) {

    assert(0 && "TODO");
#if 0
    auto &device = *cluster.get_device();
    auto folder = cluster.get_folders().by_id(folder_id);
    auto &local_files = folder->get_folder_infos().by_device(device)->get_file_infos();
    auto local_file = local_files.by_name(proto_file.name());
    assert(local_file);

    proto_file.set_sequence(0);

    initialize(cluster, sequencer, local_file->make_conflicting_name());
#endif
}

auto remote_win_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
#if 0
    LOG_TRACE(log, "visiting remote_win_t, folder = {}, file = {}", folder_id, proto_file.name());
    return visitor(*this, custom);
#endif
}
